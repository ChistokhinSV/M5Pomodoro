import json
import logging

import logging
logger = logging.getLogger(__name__)
logger.setLevel(logging.INFO)

formatter = logging.Formatter('[%(levelname)s | %(name)s:%(lineno)d] %(message)s')
handler = logging.StreamHandler()
handler.setFormatter(formatter)
logger.addHandler(handler)



def lambda_handler(event, context):
    logger.info(f'event: {event}')
    logger.info(f'context: {context}')

    for record in event['Records']:
        logger.info(f'Processing SNS record: {record}')
        record_body = record['Sns']
        message = json.loads(record_body.get('Message', None))
        payload = message.get('payload', None)
        if payload != None:
            logger.info(f'SNS message payload: {payload}')
        else:
            logger.error(f'No payload in SNS message!')
            return

        start = payload.get('start', None)
        stop = payload.get('stop', None)
        tags = payload.get('tags', [])
        if not tags: tags = []

        logger.info(f'start: {start}, stop: {stop}, tags: {tags}')

        if (start or stop) and not 'pomodoro-break' in tags:
            logger.debug(f'Updating timer')
            from mqtt_utils import update_thing_timer
            update_response = update_thing_timer()
            logger.info(f'Timer updated: {update_response}')

    logger.info(f'SNS handler OK')
    return {
        'statusCode': 200,
        'body': json.dumps('SNS handler OK')
    }