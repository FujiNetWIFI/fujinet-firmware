# WebUI

Building the fujinet now creates the web interface from template files using the jinja2 templating library.

These are deployed to the usual place for the target you are building, e.g. for `target_platform` of `BUILD_ATARI` they will be generated in `data/BUILD_ATARI`.

The configuration and templates all live under the `data/webui` directory.
During a build, files will be:

1. copied from `common` directly to the target unmodified.
2. copied from `template` dir applying any template logic to files matching `*.tmpl.*` naming convention
3. copied from `device_specific` to target unmodified depending on your `target_platform` setting.

## Existing Users - Required Changes

Existing users need to make the following change in `platformio.ini` in order to build:

```ini
extra_scripts = 
    pre:build_packages.py
    pre:build_version.py
    pre:build_webui.py
```

This is because the file is not under source control.

## Creating a new board

Follow these steps when creating config for a new board:

1. Add a new yaml for the target in `data/webui/config/<fujinet-new-board>.yaml` with appropriate switches for the sections to enable in the webUI (you can copy an existing file and change the boolean values to suit)
2. Create a directory for the device under `data/webui/device_specific/BUILD_XXX` matching the new name, and put any new files for this device only in here, or create a `.keep` file so that git will include the dir.
3. Edit the `platformio-sample.ini` adding the new board's information, and your own platformio.ini file to use it

You can now build as normal, and the new device's WebUI will be built from the templates.

## Building WebUI - More details

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

The python scripts in the build will ensure appropriate packages are installed in the PIO build dir, and then create the webUI for the current `target_platform`.

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

This directory contains files that need to be copied to all target directories. They are not templated (if required, this needs a small change to `build_webui.py`).

e.g. `atarifont.css`

## `config` directory

This directory contains the yaml files that are picked up by the build for the templating system (jinja2).

These files can be extended, and then used in templates. An example of which is:

```yaml
components:
  network: true
  # ...
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