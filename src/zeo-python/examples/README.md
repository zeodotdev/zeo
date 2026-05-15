# kicad-python Examples

Some of these examples are simple scripts that can be run standalone from a terminal or IDE,
for example:

```
python3 hello.py
```

Some of them are full plugin examples (meaning, they have a `plugin.json` file that will be
parsed by KiCad).  To use these, you can either run them directly:

```
python3 round_tracks/round_tracks_action.py
```

Or, copy or symlink the plugin folder into your KiCad plugins directory.  This directory is
platform-dependent:

| Platform | Path
|----------|------
| Windows  | `C:\Users\<username>\Documents\KiCad\9.0\plugins`
| macOS    | `/Users/<username>/Documents/KiCad/9.0/plugins`
| Linux    | `~/.local/share/KiCad/9.0/plugins`

Once you have done so, KiCad should automatically create a Python virtual environment for
each plugin and make its action available on the PCB Editor toolbar.
