import boto3
from google.oauth2 import service_account
from googleapiclient.discovery import build
import json
import os
from datetime import datetime, timedelta, timezone
import math

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

bucket_name = os.environ['BUCKET_NAME']

color_refresh = 1 # re-download google colors if it is older than 1 hour
google_colors_filename = 'google_colors.json'

def hex_to_rgb_tuple(hex_color):
    hex_color = hex_color.lstrip('#')
    return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))

def fetch_google_colors(google_client):
    # Fetch colors from the Google Calendar API
    colors_response = google_client.colors().get().execute()
    event_colors = colors_response.get('event', {})
    google_colors = {color_id: hex_to_rgb_tuple(color_definition['background']) for color_id, color_definition in event_colors.items()}
    return google_colors

def cache_google_colors(s3:boto3.client, google_colors, object_key = google_colors_filename):
    # Upload the colors to S3 for caching
    s3.put_object(Bucket=bucket_name, Key=object_key, Body=json.dumps(google_colors))

def download_google_colors(s3:boto3.client, object_key = google_colors_filename):
    # Download the colors from S3
    response = s3.get_object(Bucket=bucket_name, Key=object_key)
    google_colors_data = response['Body'].read().decode('utf-8')
    return json.loads(google_colors_data)

def get_google_colors(google_client, object_key = google_colors_filename):
    s3 = boto3.client('s3')
    # Check if the cached data exists and its last modified time
    try:
        response = s3.head_object(Bucket=bucket_name, Key=object_key)
        last_modified = response['LastModified']

        # If the cached data is older than 1 hour, update it
        if datetime.now(timezone.utc) - last_modified > timedelta(hours=color_refresh):
            logger.info("Google Colors cache is outdated, needs refresh.")
            raise Exception("Google Colors cache is outdated, needs refresh.")
        logger.info("Google Colors cache is OK")

    except Exception as e:
        logger.info(f'get_google_colors: {e}')
        # If the object does not exist or we decide to refresh it, fetch new data
        google_colors = fetch_google_colors(google_client)
        cache_google_colors(s3, google_colors)
        return google_colors

    # If the cache is up-to-date, download and use it
    google_colors = download_google_colors(s3)
    return google_colors

def hex_to_rgb(hex_color):
    # Convert hex to RGB
    hex_color = hex_color.lstrip('#')
    h_len = len(hex_color)
    return tuple(int(hex_color[i:i+h_len//3], 16) for i in range(0, h_len, h_len//3))

def color_distance(color1, color2):
    # Calculate the distance between two RGB tuples
    (r1, g1, b1) = color1
    (r2, g2, b2) = color2
    return math.sqrt((r1 - r2) ** 2 + (g1 - g2) ** 2 + (b1 - b2) ** 2)

def closest_google_color(google_client, color):
    google_colors = get_google_colors(google_client)
    logger.info(f'google_colors: {google_colors}')

    # Find the closest color in a list of colors
    color_to_find = hex_to_rgb(color)
    logger.info(f'color_to_find: {color_to_find}')

    smallest_distance = float('inf')
    for color_id, google_color_rgb in google_colors.items():
        distance = color_distance(color_to_find, google_color_rgb)
        if distance < smallest_distance:
            smallest_distance = distance
            closest_color_id = color_id
    logger.info(f'closest_color_id: {closest_color_id}, color:{google_colors[closest_color_id]}')
    return closest_color_id


def google_calendar(event, event_action = 'created'):
    from .toggl_api_utils import get_project_name_and_color
    from webhook_handler.app import region_name

    logger.info(f'google_calendar\naction: {event_action}, event body: {event}')

    # Extract start and end times from the Toggl event
    payload = event.get('payload', None)

    start_time = payload.get('start', None)
    end_time = payload.get('stop', None)
    workspace_id = payload.get('workspace_id', None)
    project_id = payload.get('project_id', None)

    if workspace_id and project_id:
        project_name, project_color = get_project_name_and_color(workspace_id, project_id)
    else:
        project_name = 'Toggl Time Entry'
        project_color = '#FF0000'

    try:
        google_calendar_id = os.getenv('GOOGLE_CALENDAR_ID')
        google_calendar_auth_secret_arn = os.getenv('GOOGLE_CALENDAR_AUTH_SECRET_ARN')
    except Exception as e:
        logger.error(f'Error getting environment variables: {e}')
        return {
            'statusCode': 500,
            'body': json.dumps('Error getting environment variables')
        }

    if (error_variable := (('TEMPLATE!' in google_calendar_id and "google_calendar_id") or
                        ('TEMPLATE!' in google_calendar_auth_secret_arn and "google_calendar_auth_secret_arn"))):
        logger.error(f'Error getting environment variable: {error_variable}')
        return {
            'statusCode': 500,
            'body': json.dumps(f'Error getting environment variable: {error_variable}.')
        }


    # Set up the Google Calendar API client
    try:
        client = boto3.client(service_name='secretsmanager', region_name=region_name)
    except Exception as e:
        logger.error(f'Error creating boto3 client: {e}')
        return {
            'statusCode': 500,
            'body': json.dumps('Error creating boto3 client')
        }

    # Get the Google Calendar API credentials from AWS secrets
    try:
        secret_response = json.loads(client.get_secret_value(SecretId=google_calendar_auth_secret_arn)['SecretString'])
        credentials = service_account.Credentials.from_service_account_info(secret_response)
    except Exception as e:
        logger.error(f'Error getting Google API credentials: {e}')
        return {
            'statusCode': 500,
            'body': json.dumps('Error getting Google API credentials, check GoogleAPICredentials secret')
        }

    try:
        service = build('calendar', 'v3', credentials=credentials)
    except Exception as e:
        logger.error(f'Error creating Google Calendar API client: {e}')
        return {
            'statusCode': 500,
            'body': json.dumps('Error creating Google Calendar API client')
        }

    event_description = payload.get('description', '')
    toggl_event_id = payload.get('id', None)
    toggl_task_id = payload.get('task_id', None)

    existing_event_id = None
    if toggl_event_id:
        events_result = service.events().list(
            calendarId=google_calendar_id,
            privateExtendedProperty=f"toggl_event_id={toggl_event_id}"
        ).execute()
        events = events_result.get('items', [])
        if not events:
            logger.info(f'No events found for toggl_event_id: {toggl_event_id}')
        else:
            # Assuming the first returned event is the one you want to update
            existing_event_id = events[0]['id']
            logger.info(f'events found for toggl_event_id: {toggl_event_id}. existing_event_id: {existing_event_id}')

    # find color for use with event
    if event_action != 'deleted':
        color_id = closest_google_color(service, project_color)
    else:
        color_id = None

    # Create a calendar event
    if len(event_description) > 0: project_name = f'{project_name} ({event_description})'
    event = {
        'summary': project_name,
        'start': {'dateTime': start_time},
        'end': {'dateTime': end_time},
        'description': event_description,
        'extendedProperties': {
            'private' : {}
        }
    }

    if workspace_id : event['extendedProperties']['private']['toggl_workspace_id'] = workspace_id
    if project_id : event['extendedProperties']['private']['toggl_project_id'] = project_id
    if toggl_event_id : event['extendedProperties']['private']['toggl_event_id'] = toggl_event_id
    if toggl_task_id : event['extendedProperties']['private']['toggl_task_id'] = toggl_task_id
    if color_id : event['colorId'] = color_id

    logger.info(f'Event for Google Calendar: {event}')

    # Call the Calendar API to create an event
    try:
        if existing_event_id and event_action == 'deleted':
            event_result = service.events().delete(calendarId=google_calendar_id, eventId=existing_event_id).execute()
        elif existing_event_id:
            event_result = service.events().update(calendarId=google_calendar_id, eventId=existing_event_id, body=event).execute()
        else:
            event_result = service.events().insert(calendarId=google_calendar_id, body=event).execute()
    except Exception as e:
        logger.error(f'Error processing Google calendar event: {e}')
        return None
    logger.info(f'{event_action} Google calendar event: {event_result}')
    return event_result