
Import("env")

try:
    import Jinja2
except ImportError:
    env.Execute("$PYTHONEXE -m pip install Jinja2")

try:
    import pyyaml
except ImportError:
    env.Execute("$PYTHONEXE -m pip install pyyaml")

