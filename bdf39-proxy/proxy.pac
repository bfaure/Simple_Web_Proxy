// file that routes HTTP requests to the proxy.exe file...

function FindProxyForURL(url, host)
{
	// the following URLs will not be proxied (for testing)
	if (dnsDomainIs(host,"google.com"))
	{
		return "DIRECT";
	}
	
	else
	{
		return "./proxy 10.92.168.0.1:8080";
	}
}