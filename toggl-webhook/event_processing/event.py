import json
import os
from datetime import datetime, timedelta
from event_processing.google_calendar_event import google_calendar

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)
formatter = logging.Formatter('[%(levelname)s | %(name)s:%(lineno)d] %(message)s')
handler = logging.StreamHandler()
handler.setFormatter(formatter)
logger.addHandler(handler)

min_event_time = int(os.environ['MIN_EVENT_TIME'])
default_event_time = int(os.environ['DEFAULT_EVENT_TIME'])

def event_processing(event, context):
    for record in event['Records']:
        # Each record will contain a message from the SQS queue
        logger.info(f'Processing record: {record}')

        record_body_decoded = json.loads(record['body'])
        toggl_event = record_body_decoded.get('Message', None)
        if toggl_event:
            toggl_event = json.loads(toggl_event)
        payload = toggl_event.get('payload', None)

        if payload is None:
            logger.error('No payload!')
            return

        event_metadata = toggl_event.get('metadata', None)
        if event_metadata:
            event_action = event_metadata.get('action', None)
        else:
            event_action = None
        logger.info(f'Message payload from queue: {payload}, action: {event_action}')

        if isinstance(payload,dict):
            start = payload.get('start', None)
            stop = payload.get('stop', None)
            duration = payload.get('duration', None)
            if duration and duration < 0:
                duration = None # if duration is negative, ignore it (ongoing)
            tags = payload.get('tags', None)
            if tags and 'pomodoro-break' in tags:
                logger.debug('Ignoring pomodoro-break event')
                return
            if start or stop: # if start or stop exist - this is a calendar event to create/update/delete
                event_result = None
                if start and stop and duration and duration >= min_event_time:
                    # if both start and stop are present and duration > min_event_time sec,
                    # create an event in Google Calendar
                    event_result = google_calendar( toggl_event, event_action)
                elif start and stop and duration and duration < min_event_time:
                    # if less than min_event_time, delete the event in Google Calendar
                    logger.debug(f'Deleting event with duration < {min_event_time} seconds')
                    event_result = google_calendar(toggl_event, 'deleted')
                elif start and stop is None: # if only start present - create default_event_time long entry
                    logger.debug(f'Creating default_event_time ({default_event_time} minutes) long event')
                    stop_time_obj = datetime.strptime(start, '%Y-%m-%dT%H:%M:%SZ')\
                        + timedelta(minutes=default_event_time)
                    payload['stop'] = stop_time_obj.strftime('%Y-%m-%dT%H:%M:%SZ')
                    event_result = google_calendar(toggl_event, event_action)
                try:
                    status_code = event_result.get('statusCode', None)
                except Exception as e:
                    logger.error(f'No status_code in event_result. Error: {e}')
                    status_code = None
                if status_code and status_code != 200:
                    logger.error(f'Error creating event: {event_result}')
                    return event_result
                if event_result is None:
                    logger.error(f'Event without event_result!\
                        action: {event_action} start:{start} stop:{stop} duration: {duration} record: {payload}')
                else:
                    logger.debug(f'Event created: {event_result}')
                return
        else:
            logger.error(f'Invalid payload type! Payload type: {type(payload)}')
            return

    logger.error('Nothing to see here! (This should never happen)')
