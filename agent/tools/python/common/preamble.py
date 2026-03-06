import sys, math

# ---------------------------------------------------------------------------
# Shared utilities prepended to every embedded tool script
# ---------------------------------------------------------------------------

def tool_log(msg):
    """Write a diagnostic message to the real stderr.

    python_exec_thread redirects sys.stderr into the captured output buffer
    so that tool results are clean JSON. Use this instead of print(...,
    file=sys.stderr) to emit diagnostics without corrupting the result.
    sys.__stderr__ always refers to the original pre-redirect stderr.
    """
    print(msg, file=sys.__stderr__)

def refresh_or_fail(api):
    """Refresh document specifier; raise if editor is not open."""
    if hasattr(api, 'refresh_document'):
        if not api.refresh_document():
            raise RuntimeError('Editor not open or document not available')

def get_pos(obj, scale=1000000):
    """Extract [x, y] in mm from various position types."""
    if obj is None:
        return [0, 0]
    if hasattr(obj, 'x') and hasattr(obj, 'y'):
        return [obj.x / scale, obj.y / scale]
    if isinstance(obj, dict):
        return [obj.get('x', 0) / scale, obj.get('y', 0) / scale]
    if isinstance(obj, (list, tuple)) and len(obj) >= 2:
        return [obj[0] / scale, obj[1] / scale]
    return [0, 0]

def snap_to_grid(mm, grid=1.27):
    """Snap a mm value to the nearest grid point (default 1.27 mm / 50 mil)."""
    grid_units = round(mm / grid)
    return round(grid_units * grid, 4)

def rotate_point(x, y, angle_deg):
    """Rotate (x, y) around the origin by angle_deg degrees."""
    if angle_deg == 0:
        return x, y
    angle_rad = math.radians(angle_deg)
    cos_a = math.cos(angle_rad)
    sin_a = math.sin(angle_rad)
    return x * cos_a - y * sin_a, x * sin_a + y * cos_a

def get_lib_id_str(sym):
    """Convert a symbol's lib_id to a plain string."""
    if not hasattr(sym, 'lib_id'):
        return ''
    lib_id = sym.lib_id
    if hasattr(lib_id, 'to_string'):
        return lib_id.to_string()
    return str(lib_id)

def get_uuid_str(obj):
    """Extract UUID string from an object."""
    if hasattr(obj, 'id'):
        return str(obj.id.value)
    return ''
