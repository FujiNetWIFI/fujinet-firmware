# pyright: reportUndefinedVariable=false

import os, glob, re, shutil, configparser
from jinja2 import Environment, FileSystemLoader
from yaml import load, Loader

def prep_dst(fname, build_platform, prefix):
    rel_path = fname.replace(prefix, '')
    destination = os.path.join('data', build_platform, rel_path)
    dest_dir = os.path.dirname(destination)
    if not os.path.exists(dest_dir):
        os.makedirs(dest_dir)
    return destination


def process_template(fname, build_platform, template_env, config, prefix):
    print(f"processing template file {fname}")
    # jinja2 insists on '/' as the path separator even on windows. see https://github.com/pallets/jinja/issues/767
    template = template_env.get_template(fname.replace(prefix, '').replace('\\', '/'))
    r = template.render(config)
    destination = prep_dst(fname, build_platform, prefix).replace('.tmpl.', '.')
    with open(destination, 'w') as f:
        f.write(r)

def copy_file(fname, build_platform, prefix):
    destination = prep_dst(fname, build_platform, prefix)
    shutil.copy(fname, destination)

Import("env")

# print(env.Dump())

target = env["PIOENV"]
# PROGRAM_ARGS is a list of args provided by pio with "-a" switch
if 'dev' in env["PROGRAM_ARGS"]:
    target = "dev"

template_env = Environment(loader=FileSystemLoader("data/webui/template"))
config = load(open(os.path.join('data', 'webui', 'config', f'{target}.yaml')), Loader=Loader)

pio_config = configparser.ConfigParser()
pio_config.read('platformio.ini')
build_platform = pio_config['fujinet']['build_platform']
print(f"Building webUI into data/{build_platform}")

if not build_platform.startswith('BUILD_'):
    raise Exception(f"build_platform does not match BUILD_*, aborting")

data_build_platform_path = os.path.join("data", build_platform)
if (os.path.isdir(data_build_platform_path)):
    shutil.rmtree(data_build_platform_path)

# copy common files not in www dir - these are files that do not need templating, e.g. binary files
common_prefix = os.path.join('data', 'webui', 'common', '')
for filename in glob.iglob(f'{common_prefix}**', recursive=True):
    if os.path.isfile(filename):
        copy_file(filename, build_platform, common_prefix)

# copy template files, rendering if name matches *.tmpl.*
template_matcher = re.compile(r'^.*\.tmpl\.[a-zA-Z0-9_]+$')
webui_template_prefix = os.path.join('data', 'webui', 'template', '')
for filename in glob.iglob(f'{webui_template_prefix}**', recursive=True):
    if os.path.isfile(filename):
        if (template_matcher.search(filename)):
            process_template(filename, build_platform, template_env, config, webui_template_prefix)
        else:
            copy_file(filename, build_platform, webui_template_prefix)

# copy additional files from appropriate BUILD_* data dir, which is stored in the ini file under fujinet.build_platform
# if there are file clashes, these will override the above, so it allows for device specific overrides

dev_specific_prefix = os.path.join('data', 'webui', 'device_specific', build_platform, '')
for filename in glob.iglob(f"{dev_specific_prefix}**", recursive=True):
    if os.path.isfile(filename) and filename != '.keep':
        copy_file(filename, build_platform, dev_specific_prefix)
