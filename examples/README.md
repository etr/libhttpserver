Example Programs
================

hello_world.cpp	- a very simple example of using libhttpserver to
		  create a Rest server capable of receiving and processing
		  HTTP requests.  The server will be listening on port
		  8080.


service.cpp	- an example using more of the libhttpserver API.
		  This creates a Rest server capable of running with
		  HTTP or HTTPS (provided that libhttpserver and
		  libmicrohttpd have been compiled with SSL support.

		  The server can be configured via command line
		  arguments:

		  -p <port>  - port number to listen on (default 8080)
		  -s         - enable HTTPS
		  -k         - server key filename (default "key.pem")
		  -c         - server certificate filename (default "cert.pem")

Creating Certificates
=====================
Self-signed certificates can be created using OpenSSL using the
following steps:

	  $ openssl genrsa -des3 -passout pass:x -out server.pass.key 2048
	  $ openssl rsa -passin pass:x -in server.pass.key -out server.key
	  $ openssl req -new -key server.key -out server.csr
	  $ openssl x509 -req -days 365 -in server.csr -signkey server.key -out server.crt

On the last step when prompted for a challenge password it can be left
empty.

Thanks to https://devcenter.heroku.com/articles/ssl-certificate-self
for these instructions.

Keystore configuration
======================
If using a local client such as RestClient
(https://github.com/wiztools/rest-client) for testing the Rest server
then a keystore needs to be established.  These commands should be
bundled with your Java installation.

$ keytool -noprompt -import -keystore /path/to/restclient.store -alias
restclient -file /path/to/server.crt

The keys in the store can be listed as follows:

$ keytool -list -v -keystore /path/to/restclient.store

The client can then be configured to use this keystore.  Thanks to
http://rubensgomes.blogspot.com/2012/01/how-to-set-up-restclient-for-ssl.html
for instructions on configuring RestClient.



