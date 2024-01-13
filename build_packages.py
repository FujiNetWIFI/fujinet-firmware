
Import("env")

try:
    import jinja2
except ImportError:
    env.Execute("$PYTHONEXE -m pip install Jinja2")

try:
    import yaml
except ImportError:
    env.Execute("$PYTHONEXE -m pip install pyyaml")

