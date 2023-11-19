import json
import logging
import os

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

formatter = logging.Formatter('[%(levelname)s | %(name)s:%(lineno)d] %(message)s')
handler = logging.StreamHandler()
handler.setFormatter(formatter)
logger.addHandler(handler)



def lambda_handler(event, context):
    # The event parameter is a dict containing the message payload
    logger.info("Received event: " + json.dumps(event))

    previous_state = event.get('previous', {}).get('state', {}).get('desired', {}).get('timer_state', '')
    new_state = event.get('current', {}).get('state', {}).get('desired', {}).get('timer_state', '')

    if previous_state and new_state and (previous_state != new_state):
        logger.info("New desired state reported: " + json.dumps(new_state))

        TOGGL_WID = int(os.getenv('TOGGL_WID'))
        from toggl_api_utils import start_timer, stop_timer

        description = ''

        # If the timer_state is 'STOPPED' - stop a timer on Toggl
        if new_state in ['STOPPED', 'PAUSED', 'REST']:
            logger.info("The device timer is now STOPPED/PAUSED/REST - stop the Toggl")
            result = stop_timer(TOGGL_WID)
            logger.info(f'stop_timer result:{result}')
        elif new_state == 'POMODORO':
            logger.info("The device timer is now POMODORO - start the Toggl")
            project_name, color = start_timer(TOGGL_WID)
            logger.info(f'start_timer {project_name}')
            description = project_name
            if not description:
                description = ''
        else:
            logger.info(f"The device timer is now {new_state} - do nothing")

        # Update the device shadow
        THING_NAME = os.getenv('THING_NAME')
        payload = {
            "state" : {
                "desired": {
                    "description" : description
                },
                "reported": {
                    "description" : description
                }
            }
        }
        logger.info(f"Updating device shadow with description payload: {payload}")
        from mqtt_utils import update_device_shadow
        update_device_shadow(THING_NAME, payload)

    # Return a success response
    return {'status': 'success'}
