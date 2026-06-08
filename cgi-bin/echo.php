<?php
$cl = (int)(getenv("CONTENT_LENGTH") ?: 0);
$body = "";
if ($cl > 0) {
    $body = fread(STDIN, $cl);
    if ($body === false) $body = "";
}

$payload = [
    "engine"          => "php",
    "method"          => getenv("REQUEST_METHOD") ?: "",
    "uri"             => getenv("REQUEST_URI") ?: "",
    "path_info"       => getenv("PATH_INFO") ?: "",
    "query"           => getenv("QUERY_STRING") ?: "",
    "content_type"    => getenv("CONTENT_TYPE") ?: "",
    "content_length"  => $cl,
    "server_protocol" => getenv("SERVER_PROTOCOL") ?: "",
    "server_name"     => getenv("SERVER_NAME") ?: "",
    "server_port"     => getenv("SERVER_PORT") ?: "",
    "host"            => getenv("HTTP_HOST") ?: "",
    "body"            => $body,
];

echo "Content-Type: application/json\r\n\r\n";
echo json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE) . "\n";
