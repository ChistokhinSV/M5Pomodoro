import json
import os
from datetime import datetime, timedelta

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

min_event_time = int(os.environ['MIN_EVENT_TIME'])
default_event_time = int(os.environ['DEFAULT_EVENT_TIME'])

def event_processing(event, context):
    for record in event['Records']:
        # Each record will contain a message from the SQS queue
        logger.info(f'Processing record: {record}')
        record_body = json.loads(record['body'])
        payload = record_body.get('payload', None)

        if payload == None:
            logger.error(f'No payload!')
            return

        event_metadata = record_body.get('metadata', None)
        if event_metadata: event_action = event_metadata.get('action', None)
        else: event_action = None
        logger.info(f'Message payload from queue: {payload}, action: {event_action}')

        if type(payload) == dict:
            start = payload.get('start', None)
            stop = payload.get('stop', None)
            duration = payload.get('duration', None)
            if start or stop: # if start or stop exist - this is a calendar event to create/update/delete
                from google_calendar_event import google_calendar
                if start and stop and duration and duration > min_event_time:
                    # if both start and stop are present and duration > min_event_time sec, create an event in Google Calendar
                    event_result = google_calendar( record_body, event_action)
                elif start and stop and duration and duration < min_event_time:
                    # if less than min_event_time, delete the event in Google Calendar
                    logger.info(f'Deleting event with duration < {min_event_time} seconds')
                    event_result = google_calendar(record_body, 'deleted')
                elif start and stop == None: # if only start present - create default_event_time long entry
                    logger.info(f'Creating default_event_time ({default_event_time} minutes) long event')
                    stop_time_obj = datetime.strptime(start, '%Y-%m-%dT%H:%M:%SZ')\
                        + timedelta(minutes=default_event_time)
                    record_body['payload']['stop'] = stop_time_obj.strftime('%Y-%m-%dT%H:%M:%SZ')
                    event_result = google_calendar(record_body, event_action)
                try:
                    status_code = event_result.get('statusCode', None)
                except:
                    status_code = None
                if status_code and status_code != 200:
                    logger.error(f'Error creating event: {event_result}')
                    return event_result
                logger.info(f'Event {event_action}: {event_result}')
                return
        else:
            logger.error(f'Invalid payload type! Payload type: {type(payload)}')
            return

    logger.error('Nothing to see here! (This should never happen)')
