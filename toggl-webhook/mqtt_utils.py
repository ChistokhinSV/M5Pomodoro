from datetime import datetime, timezone
import os
import boto3
import json
from toggl_api_utils import get_current_time_entry

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

def get_remaining_time():
    """
    Prepares shadow data for timer

    Returns:
    dict: Updated JSON payload with epoch seconds
    """
    # Extract 'start' time from the response JSON

    stop_payload = {
        "state": {
            "desired": {
                "timer_state": "STOPPED",
                "start": 0
            }
        }
    }

    response_json = get_current_time_entry()
    if response_json is None:
        return stop_payload

    if response_json.get('duration', 0) >= 0:
        return stop_payload

    start_time_str = response_json.get('start')

    # Parse the 'start' time string into a datetime object
    start_time = datetime.strptime(start_time_str, "%Y-%m-%dT%H:%M:%S%z")

    # Convert 'start' time to epoch seconds
    start_epoch = int(start_time.timestamp())

    # Prepare the payload to be sent
    payload = {
        "state": {
            "desired": {
                "timer_state": "POMODORO",
                "start": start_epoch
            }
        }
    }

    return payload

def update_device_shadow(thing_name, payload):
    """
    Updates the device shadow for the given thing with the provided payload.

    Parameters:
    thing_name (str): The name of the thing (device) whose shadow is to be updated.
    payload (dict): The payload to update the shadow with.

    Returns:
    dict: The response from the shadow update request.
    """
    if thing_name is None:
        logger.error(f"Invalid thing name ({thing_name})")
        return None

    # Create an IoT client
    client = boto3.client('iot-data')

    # Convert the payload dictionary to a JSON string
    payload_str = json.dumps(payload)

    # Update the device shadow
    response = client.update_thing_shadow(
        thingName=thing_name,
        payload=payload_str
    )

    # Read the response payload
    response_payload = json.loads(response['payload'].read())

    return response_payload

def update_thing_timer():
    # Get the thing name from the environment variable
    THING_NAME = os.getenv('THING_NAME')
    payload = get_remaining_time()
    if payload is None:
        return None
    return update_device_shadow(THING_NAME, payload)
