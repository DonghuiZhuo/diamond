package utils;

import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.nio.charset.Charset;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.apache.commons.codec.binary.Base64;
import org.apache.commons.codec.binary.StringUtils;
import org.apache.http.NameValuePair;
import org.apache.http.client.utils.URLEncodedUtils;

import com.sun.net.httpserver.Headers;

public class Utils {
	public static Map<String, String> getQueryParams(URI uri) {
		List<NameValuePair> queryParams = URLEncodedUtils.parse(uri, "UTF-8");
		Map<String, String> result = new HashMap<String, String>();
		for (NameValuePair pair : queryParams) {
			result.put(pair.getName(), pair.getValue());
		}
		return result;
	}
	
	public static String getUsername(Headers requestHeaders) {
		String authString = requestHeaders.get("Authorization").get(0).split("\\s+")[1];
		return StringUtils.newStringUtf8(Base64.decodeBase64(authString)).split(":")[0];
	}

	public static Map<String, String> getBodyParams(InputStream requestBody) {
		try {
			byte[] bodyArray = new byte[requestBody.available()];
			requestBody.read(bodyArray);
			String bodyString = new String(bodyArray, "UTF-8");
			List<NameValuePair> queryParams = URLEncodedUtils.parse(bodyString, Charset.forName("UTF-8"));
			Map<String, String> result = new HashMap<String, String>();
			for (NameValuePair pair : queryParams) {
				result.put(pair.getName(), pair.getValue());
			}
			return result;
		}
		catch (IOException e) {
			return null;
		}
	}
}
