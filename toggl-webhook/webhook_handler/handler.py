import json
import os
import boto3
import hmac

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

queue_name = os.environ['QUEUE_NAME']

def hmac_is_valid(message, signature, secret):
    '''
    Checking if signature for Toggl message is valid
    '''
    logger.debug(f'hmac_is_valid signature:{signature}')
    digest = hmac.new(secret.encode('utf-8'), message.encode('utf-8'), 'sha256').hexdigest()
    hmac_valid = hmac.compare_digest(signature, f'sha256={digest}')
    logger.debug(f'hmac_is_valid:{hmac_valid}')
    return hmac_valid

def webhook_handler(event, context):
    logger.debug(f'event: {event}')
    logger.debug(f'context: {context}')

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
    logger.debug(f'event payload: {payload}')

    validation_code = toggl_event.get('validation_code', None)
    logger.debug(f'validation_code: {validation_code}')

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

    # if it is ping - answer it, if not - queue it
    if payload == 'ping':
        logger.info('Ping!')
        return simple_ok
    else:
        logger.debug('Queueing event')
        event_data = json.dumps(toggl_event)
        # Send the event data to the SQS queue
        sqs_client = boto3.client('sqs')
        response = sqs_client.send_message(
            QueueUrl=queue_name,
            MessageBody=event_data
        )
        logger.info(f'Message sent to SQS: {response}')
        return simple_ok
