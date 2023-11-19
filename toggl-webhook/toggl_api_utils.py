import requests
import base64
import os
from datetime import datetime, timezone

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

toggl_api_url = 'https://api.track.toggl.com/api/v9'

def api_request(url:str, api_method = "GET", request_body = {}):
    toggl_api_key = os.environ['TOGGL_API_TOKEN']

    if 'TEMPLATE!' in toggl_api_key or toggl_api_key == '':
        logger.error(f'Error getting environment variable: TOGGL_API_KEY')
        return None

    toggl_api_key = base64.b64encode(f'{toggl_api_key}:api_token'.encode()).decode()

    toggl_api_headers = {
        'Content-Type': 'application/json',
        'Authorization': f'Basic {toggl_api_key}'
    }

    if url[0] != '/':  url = '/' + url

    try:
        if api_method == "POST":
            response = requests.post(f'{toggl_api_url}{url}', headers=toggl_api_headers, json=request_body)
        elif api_method == "PUT":
            response = requests.put(f'{toggl_api_url}{url}', headers=toggl_api_headers, json=request_body)
        else:
            response = requests.get(f'{toggl_api_url}{url}', headers=toggl_api_headers)
        return response
    except Exception as e:
        logger.error(f'Error making Toggl API request ({api_method}): {e}')
        return None

def get_project_name_and_color(workspace_id, project_id):
    logger.debug(f'get_project_name. workspace_id: {workspace_id} project_id: {project_id}')
    response = api_request(f'/workspaces/{workspace_id}/projects/{project_id}')
    if response is None:
        return None, None

    logger.debug(f'get_project_name response:{response.status_code}, {response.text}')

    project_name = response.json().get('name', None)
    project_color = response.json().get('color', None)
    return project_name, project_color

def get_current_time_entry():
    logger.debug(f'get_current_time_entry')
    response = api_request(f'me/time_entries/current')
    if response is None:
        return None

    logger.info(f'get_current_time_entry response:{response.status_code}, {response.text}')

    time_entry = response.json()
    if time_entry and time_entry.get('duration', 0) < 0: # for ongoing entries duration is negative
        return time_entry
    else:
        return None

def format_time_epoch(time = None):
    if time:
        time_str = datetime.fromtimestamp(time, timezone.utc).strftime("%Y-%m-%dT%H:%M:%S%z")
    else:
        time_str = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S%z")
    time_str = time_str[:-2] + ':' + time_str[-2:]
    return time_str

def stop_timer(workspace_id:int, stop_time = None):
    current_timer = get_current_time_entry()
    if not current_timer:
        logger.info(f'No timer running')
        return
    stop_time_str = format_time_epoch(stop_time)
    request_body = {
    "workspace_id": workspace_id,
    # "start": "2023-11-18T04:05:22+00:00",
    "stop": stop_time_str,
    "id": current_timer["id"],
    # "duration": -1,
    "created_with" : "M5Pomodoro",
    # "description": current_timer["description"],
    }
    logger.info(f'stop_timer request:{request_body}')
    response = api_request(f'workspaces/{workspace_id}/time_entries/{current_timer["id"]}', "PUT", request_body)
    logger.info(f'stop_timer response:{response.status_code}, {response.text}')
    if response.status_code == 200:
        return response.json()
    else:
        return None

def start_timer(workspace_id:int, start_time = None):
    current_timer = get_current_time_entry()
    if current_timer:
        logger.info(f'Timer already running, no need to restart it (?)')
        # stop_timer(workspace_id)
        return
    start_time_str = format_time_epoch(start_time)
    request_body = {
    "workspace_id": workspace_id,
    "start": start_time_str,
    "stop": None,
    "duration": -1,
    "created_with" : "M5Pomodoro",
    "description": "Hardware timer entry",
    }
    response = api_request(f'workspaces/{workspace_id}/time_entries', "POST", request_body)
    logger.info(f'start_timer response:{response.status_code}, {response.text}')
    if response.status_code == 200:
        return response.json()
    else:
        return None