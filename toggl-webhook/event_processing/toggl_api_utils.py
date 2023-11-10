import requests
import base64
import os
import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

toggl_api_url = 'https://api.track.toggl.com/api/v9'

def get_project_name_and_color(workspace_id, project_id):
    logger.info(f'get_project_name\nworkspace_id: {workspace_id} project_id: {project_id}')

    toggl_api_key = os.environ['TOGGL_API_TOKEN']

    if 'TEMPLATE!' in toggl_api_key or toggl_api_key == '':
        logger.error(f'Error getting environment variable: TOGGL_API_KEY')
        return None, None

    # encode toggl_api_ley into base64
    toggl_api_key = base64.b64encode(f'{toggl_api_key}:api_token'.encode()).decode()

    toggl_api_headers = {
        'Content-Type': 'application/json',
        'Authorization': f'Basic {toggl_api_key}'
    }

    # Get the project name from Toggl
    try:
        response = requests.get(f'{toggl_api_url}/workspaces/{workspace_id}/projects/{project_id}', headers=toggl_api_headers)
        logger.info(f'get_project_name response:{response.status_code}, {response.text}')
        project_name = response.json().get('name', None)
        project_color = response.json().get('color', None)
        return project_name, project_color
    except Exception as e:
        logger.error(f'Error getting project name from Toggl: {e}')
        return None, None