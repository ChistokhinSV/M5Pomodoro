import json
import os
from datetime import datetime, timedelta
from .google_calendar_event import google_calendar
from .toggl_api_utils import hmac_is_valid

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

region_name = "ap-southeast-1" # region, storing AWS secrets
min_event_time = 300 # 5 minutes
default_event_time = 25 # 25 minutes. If no end time specified - assume it is pomodoro

def lambda_handler(event, context):
    logger.info(f'event: {event}')
    logger.info(f'context: {context}')

    toggl_event = json.loads(event['body'])
    headers = event.get('headers', None)
    signature = headers.get('x-webhook-signature-256', None) if headers else None
    event_metadata = toggl_event.get('metadata', None)
    if event_metadata: event_action = event_metadata.get('action', None)
    else: event_action = None
    logger.info(f'toggl_event: {toggl_event}, signature: {signature}, action: {event_action}')

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
        if start or stop: # if start or stop exist - this is a calendar event to create/update/delete
            if start and stop and duration and duration > min_event_time:
                # if both start and stop are present and duration > min_event_time sec, create an event in Google Calendar
                event_result = google_calendar( toggl_event, event_action)
            elif start and stop and duration and duration < min_event_time:
                # if less than min_event_time, delete the event in Google Calendar
                logger.info(f'Deleting event with duration < {min_event_time} seconds')
                event_result = google_calendar(toggl_event, 'deleted')
            elif start and stop == None: # if only start present - create default_event_time long entry
                logger.info(f'Creating default_event_time ({default_event_time} minutes) long event')
                stop_time_obj = datetime.strptime(start, '%Y-%m-%dT%H:%M:%SZ')\
                    + timedelta(minutes=default_event_time)
                toggl_event['payload']['stop'] = stop_time_obj.strftime('%Y-%m-%dT%H:%M:%SZ')
                event_result = google_calendar(toggl_event, event_action)
            try:
                status_code = event_result.get('statusCode', None)
            except:
                status_code = None
            if status_code and status_code != 200:
                logger.error(f'Error creating event: {event_result}')
                return event_result
            logger.info(f'Event {event_action}: {event_result}')
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
