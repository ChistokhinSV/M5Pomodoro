import requests
import base64
import os
import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

toggl_api_url = 'https://api.track.toggl.com/api/v9'

def api_request(url:str):
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
        response = requests.get(f'{toggl_api_url}{url}', headers=toggl_api_headers)
        return response
    except Exception as e:
        logger.error(f'Error getting toggl data: {e}')
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

    logger.debug(f'get_current_time_entry response:{response.status_code}, {response.text}')

    time_entry = response.json()
    if time_entry and time_entry.get('duration', 0) < 0: # for ongoing entries duration is negative
        return time_entry
    else:
        return None

