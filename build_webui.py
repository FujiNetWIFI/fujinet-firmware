# pyright: reportUndefinedVariable=false

import os, glob, re, shutil, sys
from jinja2 import Environment, FileSystemLoader
from yaml import load, Loader

def prep_dst(fname):
    rel_path = fname.replace('data/webui/template/', '')
    destination = f"data/build/{rel_path}"
    dest_dir = os.path.dirname(destination)
    if not os.path.exists(dest_dir):
        os.makedirs(dest_dir)
    return destination


def process_template(fname, template_env, data):
    print(f"processing template file {fname}")
    template = template_env.get_template(fname.replace('data/webui/template/', ''))
    r = template.render(data)
    destination = prep_dst(fname).replace('.tmpl.', '.')
    with open(destination, 'w') as f:
        f.write(r)

def copy_file(fname):
    print(f"copying normal file {fname}")
    destination = prep_dst(fname)
    shutil.copy(fname, destination)

print("building webUI")
Import("env")
target = env["PIOENV"]
# PROGRAM_ARGS is a list of args provided by pio with "-a" switch
if 'dev' in env["PROGRAM_ARGS"]:
    target = "dev"

template_env = Environment(loader=FileSystemLoader("data/webui/template"))
data = load(open(f"data/webui/data/{target}.yaml"), Loader=Loader)

if (os.path.isdir('data/build')):
    shutil.rmtree("data/build")

template_matcher = re.compile(r'^.*\.tmpl\.[a-zA-Z0-9_]+$')
for filename in glob.iglob('data/webui/template/**', recursive=True):
    if os.path.isfile(filename):
        if (template_matcher.search(filename)):
            process_template(filename, template_env, data)
        else:
            copy_file(filename)
