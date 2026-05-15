# Installing dependencies

You will need the Poetry dependency management tool as well as a compatible version of the
Protocol Buffers compiler (`protoc`).  At the moment, `kicad-python` requires `protoc` version
25.3 or older.  You can obtain it from the [protobuf releases page](https://github.com/protocolbuffers/protobuf/releases/tag/v25.3)
if it is not available from your platform's package manager.  Make sure `protoc` or `protoc.exe` is
in your PATH.

# Building kicad-python

First, run `git submodule update --init` to add KiCad's source code as a submodule.

NOTE: We recommend that you run `git config submodule.recurse true` in this repo so that
      when you run `git pull` in the future, the submodule will be kept up-to-date.

Next install protobuf-compiler

then, install `poetry` and use it to install the required Python dependencies
(you may need to use `python` instead of `python3` on some platforms)

Option 1: install `poetry` globally and then use `poetry shell` to create a virtual Python
environment for `kicad-python` development:

```sh
$ sudo apt install python3-poetry # Or whatever other way you want to install Poetry
$ poetry shell
$ poetry install
```

Option 2: use some tool other than Poetry to manage your Python virtual environments:

```sh
$ python3 -m venv .env
$ . .env/bin/activate
$ python3 -m pip install --upgrade pip
$ python3 -m pip install poetry pre-commit
$ poetry install
```

Then, to build the library and install it into the local environment:

```sh
$ poetry env activate   # If you didn't run `poetry shell` earlier
$ poetry build
$ pip install -e .
```

# Running examples

With KiCad running and the API server enabled in Preferences > Plugins, you should be able to run:

```sh
$ python3 examples/hello.py
```

NOTE: On some platforms, including Windows, you should use `python` instead of `python3`.

This will work if you have a KiCad instance running, with the API server enabled,
and the server is listening at the default location (which will be the case if there
are no other instances of KiCad open at the time).

# Testing changes

Before committing, run `nox`, which will run checks such as linting.

We'll eventually add tests which will run here too :)
