# Copyright (C) 2026, Zeo <team@zeo.dev>

import json

try:
    board.save()
    output = {'status': 'success', 'doc_type': 'pcb'}
except Exception as exc:
    import traceback
    output = {
        'status': 'error',
        'message': str(exc) or repr(exc),
        'exception_type': type(exc).__name__,
        'traceback': traceback.format_exc(),
    }

print(json.dumps(output, indent=2))
