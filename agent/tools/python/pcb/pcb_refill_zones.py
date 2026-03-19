import json
from time import sleep
from kipy.errors import ApiError
from kipy.proto.common.envelope_pb2 import ApiStatusCode

# Retry if KiCad is busy (e.g. from a prior operation)
for attempt in range(5):
    try:
        board.zones.refill(block=True, max_poll_seconds=60.0)
        print(json.dumps({"status": "ok", "message": "All zones refilled successfully"}))
        break
    except ApiError as e:
        if e.code == ApiStatusCode.AS_BUSY and attempt < 4:
            sleep(1)
            continue
        raise
