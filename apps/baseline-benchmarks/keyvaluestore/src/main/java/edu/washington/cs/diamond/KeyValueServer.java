package edu.washington.cs.diamond;

import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.*;
import java.util.*;

import javax.servlet.ServletException;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import org.eclipse.jetty.server.Request;
import org.eclipse.jetty.server.Server;
import org.eclipse.jetty.server.handler.AbstractHandler;

import redis.clients.jedis.*;

public class KeyValueServer {
	JedisPool pool;
	int numSlaves;
	int numFailures;

	class PutGetHandler extends AbstractHandler {

		@Override
		public void handle(String target, Request baseRequest, HttpServletRequest request, HttpServletResponse response)
				throws IOException, ServletException {

			JsonObject responseJson = new JsonObject();
			int responseCode = HttpServletResponse.SC_BAD_REQUEST;

			if (target.equals("/put")) {
				Map<String, String> bodyParams = Utils.getBodyParams(request.getInputStream());
				String key = bodyParams.get("key");
				String value = bodyParams.get("value");

				if (key != null && value != null) {
					try(Jedis jedis = pool.getResource()) {
						jedis.set(key, value);
						jedis.waitReplicas(numSlaves - numFailures, 10000);
					}
					responseJson.add("key", new JsonPrimitive(key));
					responseJson.add("value", new JsonPrimitive(value));
					responseCode = HttpServletResponse.SC_OK;
				}
			}
			else if (target.equals("/get")) {
				String queryString = request.getQueryString();
				if (queryString == null) {
					queryString = "";
				}
				Map<String, String> queryParams = Utils.getQueryParams(queryString);
				String key = queryParams.get("key");
				String value = null;

				if (key != null) {
					try(Jedis jedis = pool.getResource()) {
						value = jedis.get(key);
					}
					if (value == null) {
						value = "";
					}
					responseJson.add("key", new JsonPrimitive(key));
					responseJson.add("value", new JsonPrimitive(value));
					responseCode = HttpServletResponse.SC_OK;
				}
			}

			response.setStatus(responseCode);
			PrintWriter out = response.getWriter();
			out.print(responseJson.toString());

			baseRequest.setHandled(true);
		}
	}

	public void start(int port, String redisHostname, int redisPort, int numSlaves, int numFailures) {
		this.numSlaves = numSlaves;
		this.numFailures = numFailures;
		
		pool = new JedisPool(new JedisPoolConfig(), redisHostname, redisPort);
		Server server = null;

		try {
			server = new Server(port);
			server.setHandler(new PutGetHandler());
			server.start();
			server.join();
		}
		catch (Exception e) {
			e.printStackTrace();
			System.exit(1);
		}
		pool.destroy();
	}

	public static void main(String[] args) {
		if (args.length < 5) {
			System.err.println("usage: java KeyValueServer port redis-hostname redis-port num-slaves num-failures");
			System.exit(1);
		}
		new KeyValueServer().start(Integer.parseInt(args[0]), args[1], Integer.parseInt(args[2]), Integer.parseInt(args[3]), Integer.parseInt(args[4]));
	}
}
