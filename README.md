# dump1090-mutability Debian/Raspbian packages
[![Build Status](https://travis-ci.org/mutability/dump1090.svg?branch=master)](https://travis-ci.org/mutability/dump1090)

This is a fork of MalcolmRobb's version of dump1090
that adds new functionality and is designed to be built as
a Debian/Raspbian package.

This version is licensed under the GPL (v2 or later).
See the file COPYING for details.

# Features

* 2.4MHz "oversampling" support
* doesn't run as root
* supports FlightAware-TSV-format connections directly (same as the FlightAware version - no faup1090 needed)
* can start from init.d, with detailed config via debconf or `/etc/default/dump1090-mutability`
* can serve the virtual radar map via an external webserver (lighttpd integration included by default)
* map view uses receiver lat/long given to dump1090 automatically
* somewhat cleaned-up network code
* tries to do things "the debian way" when it comes to config, package structure, etc
* probably a bunch of other things I've forgotten..

# A note on maps

Previously, the dump1090 webmap used Google's map API. As of July 2016, Google's policy on keyless use of their
API has changed and it's no longer practical to use that API. To avoid having a completely nonfunctional
map on new installs that have not been grandfathered, dump1090 now uses the OpenLayers map API.

This means:

* The default view now uses OpenStreetMap tiles;
* Google's maps are not available even with an API key (Google does not allow use of their imagery via
  third-party APIs such as OpenLayers);
* There are a couple of new layers - Bing and Mapzen - that can be enabled by providing an API key
  in config.js. See the comments in config.js for details.

# Manual installation

To install from packages directly:

You will need a librtlsdr0 package for Raspbian.
There is no standard build of this.
I have built suitable packages that are available from 
[this release page](https://github.com/mutability/librtlsdr/releases)

Then you will need the dump1090-mutability package itself from
[this release page](https://github.com/mutability/dump1090/releases)

Install the packages with dpkg.

# Configuration

By default it'll only ask you whether to start automatically and assume sensible defaults for everything else.
Notable defaults that are perhaps not what you'd first expect:

* All network ports are bound to the localhost interface only.
  If you need remote access to the ADS-B data ports, you will want to change this to bind to the wildcard address.
* The internal HTTP server is disabled. I recommend using an external webserver (see below).
  You can reconfigure to enable the internal one if you don't want to use an external one.

To reconfigure, either use `dpkg-reconfigure dump1090-mutability` or edit `/etc/default/dump1090-mutability`. Both should be self-explanatory.

## External webserver configuration

This is the recommended configuration; a dedicated webserver is almost always going to be better and more secure than the collection of hacks that is the dump1090 webserver.
It works by having dump1090 write json files to a path under `/run` once a second (this is on tmpfs and will not write to the sdcard).
Then an external webserver is used to serve both the static html/javascript files making up the map view, and the json files that provide the dynamic data.

The package includes a config file for lighttpd (which is what I happen to use on my system).
To use this:

````
# apt-get install lighttpd         # if you don't have it already
# lighty-enable-mod dump1090
# service lighttpd force-reload
````

This uses a configuration file installed by the package at `/etc/lighttpd/conf-available/89-dump1090.conf`.
It makes the map view available at http://<pi address>/dump1090/

This should also work fine with other webservers, you will need to write a similar config to the lighttpd one (it's basically just a couple of aliases).
If you do set up a config for something else, please send me a copy so I can integrate it into the package!

## Logging

The default configuration logs to `/var/log/dump1090-mutability.log` (this can be reconfigured).
The only real logging other than any startup problems is hourly stats.
There is a logrotate configuration installed by the package at `/etc/logrotate.d/dump1090-mutability` that will rotate that logfile weekly.

# Bug reports, feedback etc

Please use the [github issues page](https://github.com/mutability/dump1090/issues) to report any problems.
Or you can [email me](mailto:oliver@mutability.co.uk).

# Future plans

Packages following the same model for MalcolmRobb & FlightAware's forks of dump1090 are in the pipeline.
So is a repackaged version of piaware.

# Building from source

While there is a Makefile that you can use, the preferred way to build is via the Debian package building system:

````
$ sudo apt-get install librtlsdr-dev libusb-1.0-0-dev pkg-config debhelper
$ dpkg-buildpackage -b
````

Or you can use debuild/pdebuild. I find building via qemubuilder quite effective for building images for Raspbian (it's actually faster to build on an emulated ARM running on my PC than to build directly on real hardware).

Here's the pbuilder config I use to build the Raspbian packages:

````
MIRRORSITE=http://mirrordirector.raspbian.org/raspbian/
PDEBUILD_PBUILDER=cowbuilder
BASEPATH=/var/cache/pbuilder/armhf-raspbian-wheezy-base.cow
DISTRIBUTION=wheezy
OTHERMIRROR="deb http://repo.mutability.co.uk/raspbian wheezy rpi"
ARCHITECTURE=armhf
DEBOOTSTRAP=qemu-debootstrap
DEBOOTSTRAPOPTS="--variant=buildd --keyring=/usr/share/keyrings/raspbian-archive-keyring.gpg"
COMPONENTS="main contrib non-free rpi"
EXTRAPACKAGES="eatmydata debhelper fakeroot"
ALLOWUNTRUSTED="no"
APTKEYRINGS=("/home/oliver/ppa/mutability.gpg")
````
 
>**Note about Bias-t support:**
 Bias-t support is available for RTL-SDR.com V3 dongles. If you wish to enable bias-t support, you must insure that you are building this package with a version of librtlsdr that supports this capability. You can find suitable source packages [here](https://github.com/rtlsdrblog/rtl_biast) and [here](https://github.com/librtlsdr/librtlsdr/tree/master/src). To enable the necessary support code when building, be sure to include preprocessor define macro HAVE_RTL_BIAST.
