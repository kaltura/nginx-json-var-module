# Build Nginx json variables

Adds the ability to group Nginx variable expressions as json.

## Build

To link statically against nginx, cd to nginx source directory and execute:

    ./configure --add-module=/path/to/nginx-json-var-module

To compile as a dynamic module (nginx 1.9.11+), use:
  
	./configure --add-dynamic-module=/path/to/nginx-json-var-module

In this case, the `load_module` directive should be used in nginx.conf to load the module.

## Configuration

### json_var
* **syntax**: `json_var $variable { ... }`
* **default**: `none`
* **context**: `http`

Creates a new variable whose value is a json containing the items listed within the block.
Parameters inside the `json_var` block specify a field that should be included in the resulting json.
Each parameter has to contain two arguments - key and value. 
The value can contain nginx variables.

## Sample configuration
```
http {
	json_var $output {
		timestamp $time_local;
		remoteAddr $remote_addr;
		xForwardedFor $http_x_forwarded_for;
		userAgent $http_user_agent;
		params $args;
	}
	
	server {
		location /get_json/ {
			return 200 $output;
		}
	}
```
Hitting `http://domain/get_json/?key1=value1&key2=value2` can return a json like:
```
{
	"timestamp": "21/Jul/2017:12:44:18 -0400",
	"remoteAddr": "127.0.0.1",
	"xForwardedFor": "",
	"userAgent": "curl/7.22.0 (x86_64-pc-linux-gnu) libcurl/7.22.0 OpenSSL/1.0.1 zlib/1.2.3.4 libidn/1.23 librtmp/2.3",
	"params": "key1=value1&key2=value2"
}
```

## Copyright & License

All code in this project is released under the [AGPLv3 license](http://www.gnu.org/licenses/agpl-3.0.html) unless a different license for a particular library is specified in the applicable library path. 

Copyright © Kaltura Inc. All rights reserved.
