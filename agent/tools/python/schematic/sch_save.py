import json

refresh_or_fail(sch)

try:
    sch.save()
    output = {'status': 'success', 'doc_type': 'sch'}
except Exception as exc:
    import traceback
    output = {
        'status': 'error',
        'message': str(exc) or repr(exc),
        'exception_type': type(exc).__name__,
        'traceback': traceback.format_exc(),
    }

print(json.dumps(output, indent=2))
