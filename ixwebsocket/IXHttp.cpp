/*
 *  IXHttp.cpp
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#include "IXHttp.h"
#include "IXCancellationRequest.h"
#include "IXSocket.h"

#include <sstream>
#include <vector>

namespace ix
{
    std::string Http::trim(const std::string& str)
    {
        std::string out;
        for (auto c : str)
        {
            if (c != ' ' && c != '\n' && c != '\r')
            {
                out += c;
            }
        }

        return out;
    }

    std::tuple<std::string, std::string, std::string> Http::parseRequestLine(const std::string& line)
    {
        // Request-Line   = Method SP Request-URI SP HTTP-Version CRLF
        std::string token;
        std::stringstream tokenStream(line);
        std::vector<std::string> tokens;

        // Split by ' '
        while (std::getline(tokenStream, token, ' '))
        {
            tokens.push_back(token);
        }

        std::string method;
        if (tokens.size() >= 1)
        {
            method = trim(tokens[0]);
        }

        std::string requestUri;
        if (tokens.size() >= 2)
        {
            requestUri = trim(tokens[1]);
        }

        std::string httpVersion;
        if (tokens.size() >= 3)
        {
            httpVersion = trim(tokens[2]);
        }

        return std::make_tuple(method, requestUri, httpVersion);
    }

    std::tuple<bool, std::string, HttpRequestPtr> Http::parseRequest(std::shared_ptr<Socket> socket)
    {
        HttpRequestPtr httpRequest;

        std::atomic<bool> requestInitCancellation(false);

        int timeoutSecs = 5; // FIXME

        auto isCancellationRequested =
            makeCancellationRequestWithTimeout(timeoutSecs, requestInitCancellation);

        // Read first line
        auto lineResult = socket->readLine(isCancellationRequested);
        auto lineValid = lineResult.first;
        auto line = lineResult.second;

        if (!lineValid)
        {
            return std::make_tuple(false, "Error reading HTTP request line", httpRequest);
        }

        // Parse request line (GET /foo HTTP/1.1\r\n)
        auto requestLine = Http::parseRequestLine(line);
        auto method      = std::get<0>(requestLine);
        auto uri         = std::get<1>(requestLine);
        auto httpVersion = std::get<2>(requestLine);

        // Retrieve and validate HTTP headers
        auto result = parseHttpHeaders(socket, isCancellationRequested);
        auto headersValid = result.first;
        auto headers = result.second;

        if (!headersValid)
        {
            return std::make_tuple(false, "Error parsing HTTP headers", httpRequest);
        }

        httpRequest = std::make_shared<HttpRequest>(uri, method, httpVersion, headers);
        return std::make_tuple(true, "", httpRequest);
    }

    bool Http::sendResponse(HttpResponsePtr response, std::shared_ptr<Socket> socket)
    {
        // Write the response to the socket
        std::stringstream ss;
        ss << "HTTP/1.1 ";
        ss << response->statusCode;
        ss << " ";
        ss << response->description;
        ss << "\r\n";

        if (!socket->writeBytes(ss.str(), nullptr))
        {
            return false;
        }

        // Write headers
        ss.str("");
        ss << "Content-Length: " << response->payload.size() << "\r\n";
        for (auto&& it : response->headers)
        {
            ss << it.first << ": " << it.second << "\r\n";
        }
        ss << "\r\n";

        if (!socket->writeBytes(ss.str(), nullptr))
        {
            return false;
        }

        return response->payload.empty()
            ? true
            : socket->writeBytes(response->payload, nullptr);
    }
}
