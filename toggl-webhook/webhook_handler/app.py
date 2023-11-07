import json
import boto3
import os
import hmac
import base64

from google.oauth2 import service_account
from googleapiclient.discovery import build

import logging
import requests
logger = logging.getLogger()
logger.setLevel(logging.INFO)

region_name = "ap-southeast-1"
min_event_time = 60

def get_project_name(workspace_id, project_id):
    logger.info(f'get_project_name\nworkspace_id: {workspace_id} project_id: {project_id}')

    try:
        toggl_api_key = os.getenv('TOGGL_API_TOKEN')
    except Exception as e:
        logger.error(f'Error getting environment variables: {e}')
        return None

    if 'TEMPLATE!' in toggl_api_key or toggl_api_key == '':
        logger.error(f'Error getting environment variable: TOGGL_API_KEY')
        return None

    # encode toggl_api_ley into base64
    toggl_api_key = base64.b64encode(f'{toggl_api_key}:api_token'.encode()).decode()

    toggl_api_url = 'https://api.track.toggl.com/api/v9'
    toggl_api_headers = {
        'Content-Type': 'application/json',
        'Authorization': f'Basic {toggl_api_key}'
    }

    # Get the project name from Toggl
    try:
        response = requests.get(f'{toggl_api_url}/workspaces/{workspace_id}/projects/{project_id}', headers=toggl_api_headers)
        logger.info(f'get_project_name response:{response.status_code}, {response.text}')
        project_name = response.json().get('name', None)
        return project_name
    except Exception as e:
        logger.error(f'Error getting project name from Toggl: {e}')
        return None

def create_calendar(event):
    logger.info(f'create_calendar\nevent body: {event}')

    # Extract start and end times from the Toggl event
    payload = event.get('payload', None)

    start_time = payload.get('start', None)
    end_time = payload.get('stop', None)
    workspace_id = payload.get('workspace_id', None)
    project_id = payload.get('project_id', None)

    if workspace_id and project_id: project_name = get_project_name(workspace_id, project_id)
    else: project_name = 'Toggl Time Entry'

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

    toggl_event_id = payload.get('id', None)
    toggl_task_id = payload.get('task_id', None)

    if workspace_id : event['extendedProperties']['private']['workspace_id'] = workspace_id
    if project_id : event['extendedProperties']['private']['project_id'] = project_id
    if toggl_event_id : event['extendedProperties']['private']['toggl_event_id'] = toggl_event_id
    if toggl_task_id : event['extendedProperties']['private']['toggl_task_id'] = toggl_task_id

    logger.info(f'event for creation: {event}')

    # Call the Calendar API to create an event
    try:
        event_result = service.events().insert(calendarId=google_calendar_id, body=event).execute()
    except Exception as e:
        logger.error(f'Error creating Google calendar event: {e}')
        return {
            'statusCode': 500,
            'body': json.dumps('Error creating event')
        }
    return event_result

def hmac_is_valid(message, signature, secret):
    logger.info(f'hmac_is_valid signature:{signature}')
    digest = hmac.new(secret.encode('utf-8'), message.encode('utf-8'), 'sha256').hexdigest()
    hmac_valid = hmac.compare_digest(signature, f'sha256={digest}')
    logger.info(f'hmac_is_valid:{hmac_valid}')
    return hmac_valid

def lambda_handler(event, context):
    logger.info(f'event: {event}')
    logger.info(f'context: {context}')

    toggl_event = json.loads(event['body'])
    headers = event.get('headers', None)
    signature = headers.get('x-webhook-signature-256', None) if headers else None
    logger.info(f'toggl_event: {toggl_event}')
    logger.info(f'signature: {signature}')

    try: signature_env = os.getenv('TOGGL_SIGNATURE')
    except Exception as e:
        signature_env = None
        logger.ingo(f'No TOGGL_SIGNATURE found, validation disabled!')
    if signature_env and len(signature_env) == 0: signature_env = None

    # there is setup TOGGL_SIGNATURE but signature is not included
    if not signature and signature_env:
        logger.error(f'No signature included, aborting')
        return {
            'statusCode': 401,
            'body': json.dumps('No signature included')
        }

    # there is setup TOGGL_SIGNATURE but signature is not valid
    if signature_env and not hmac_is_valid(event['body'], signature, signature_env):
        logger.error(f'Invalid signature, aborting')
        return {
            'statusCode': 401,
            'body': json.dumps('Invalid signature')
        }

    payload = toggl_event.get('payload', None)
    if payload and type(payload) == str: payload = payload.lower()
    logger.info(f'payload_type: {payload}')

    validation_code = toggl_event.get('validation_code', None)
    logger.info(f'validation_code: {validation_code}')

    if validation_code:
        simple_ok = {
                'statusCode': 200,
                'body': json.dumps({ 'validation_code' : validation_code })
            }
    else:
        simple_ok = {
                'statusCode': 200,
                'body': json.dumps('OK')
            }

    # different actions for payloads
    if payload == 'ping':
        logger.info('Ping!')
        return simple_ok

    if type(payload) == dict:
        start = payload.get('start', None)
        stop = payload.get('stop', None)
        duration = payload.get('duration', None)
        if start and stop and duration and duration > min_event_time:
            # if both start and stop are present and duration > min_event_time sec, create an event in Google Calendar
            event_result = create_calendar(toggl_event)
            status_code = event_result.get('statusCode', 500)
            if status_code != 200:
                logger.error(f'Error creating event: {event_result}')
                return event_result
            logger.info(f'Event created: {event_result}')
            return simple_ok

    if payload == None:
        return {
            'statusCode': 500,
            'body': json.dumps('Error parsing event')
        }

    logger.info('Nothing to see here!')
    return {
        'statusCode': 200,
        'body': 'Something went wrong. No details'
    }
