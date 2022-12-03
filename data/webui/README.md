# Building WebUI

jinja2 is a templating system used to differentiate different device builds for the WebUI.

The `data` directory contains the following to support the templating build:

```
webui/
  common/            # files that will be copied on all platforms into WebUI
  config/            # device specific yaml files to configure the template files
  device_specific/   # contains subdirs for each BUILD_* target to copy into WebUI for this specific device
  template/
    www/             # The webUI templates and files to copy to the data/BUILD_<TARGET> directory
```

The only change existing users need to build are the following changes in `platformio.ini`.

These are in the sample file, but existing users will need to copy this section to be able to build (as this file is not under source control):

```ini
extra_scripts = 
    pre:build_packages.py
    pre:build_version.py
    pre:build_webui.py
```

The above python scripts will ensure appropriate packages are installed in the PIO build dir, and then create the webUI for the current `target_platform`.

For example, for `BUILD_ATARI`, it will create

```text
data/BUILD_ATARI
  www/
    index.html
  f/
  ...
```

which can then be used to upload to FujiNet device to replace the WebUI (using `Build Filesystem Image` or `updatefs` with CLI).

## `common` directory

This directory contains files that need to be copied to all target directories.
e.g. `atarifont.css`

## `config` directory

This directory contains the yaml files that are picked up by the build for the templating system (jinja2).

These files can be extended, and then used in templates. An example of which is:

```yaml
components:
  network: true
```

This is used in (for example) index.tmpl.html:

```html
{% if components.network %}
... html code to include if compontents.network is set to trunm
{% endif %}
```

Currently, the yaml defines booleans that control which settings blocks are included in the WebUI, e.g. for APPLE does not have a HSIO section.

## device_specific

These files are copied into the target build directory, and are specific to the `target_platform` you are building

For example:

```text
data/webui/device_specific/BUILD_ATARI/
  850handler.bin
  // ...
  picoboot.bin
```

## template directory

This contains the webUI files that either need to be copied or converted (using jinja2) using the config yaml files.

Any file with the name `*.tmpl.*` will be passed to jinja2 to convert to a file with the `.tmpl` part removed.

## build_webui.py

This script is the core of running the jinja templating, and copying various files to the `data/<TARGET>` directory.