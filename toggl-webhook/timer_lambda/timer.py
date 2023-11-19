import json

import logging
import os
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)


def lambda_handler(event, context):
    # The event parameter is a dict containing the message payload
    logger.info("Received event: " + json.dumps(event))

    previous_state = event.get('previous', {}).get('state', {}).get('desired', {}).get('timer_state', '')
    new_state = event.get('current', {}).get('state', {}).get('desired', {}).get('timer_state', '')

    if previous_state and new_state and (previous_state != new_state):
        logger.info("New desired state reported: " + json.dumps(new_state))

        TOGGL_WID = int(os.getenv('TOGGL_WID'))
        from toggl_api_utils import start_timer, stop_timer

        # If the timer_state is 'STOPPED' - stop a timer on Toggl
        if new_state in ['STOPPED', 'PAUSED', 'REST']:
            logger.info("The device timer is now STOPPED/PAUSED/REST - stop the Toggl")
            result = stop_timer(TOGGL_WID)
            logger.info(f'stop_timer result:{result}')
        elif new_state == 'POMODORO':
            logger.info("The device timer is now POMODORO - start the Toggl")
            result = start_timer(TOGGL_WID)
            logger.info(f'start_timer result:{result}')

    # Return a success response
    return {'status': 'success'}
