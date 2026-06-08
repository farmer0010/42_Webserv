<?php
echo "Content-Type: text/plain; charset=UTF-8\r\n\r\n";

echo "=== PHP info ===\n";
echo "php_version=" . phpversion() . "\n";
echo "sapi=" . php_sapi_name() . "\n\n";

echo "=== CGI environment (getenv) ===\n";
$keys = [
    "REQUEST_METHOD","REQUEST_URI","PATH_INFO","QUERY_STRING",
    "CONTENT_TYPE","CONTENT_LENGTH","SERVER_PROTOCOL",
    "SERVER_NAME","SERVER_PORT","HTTP_HOST","HTTP_USER_AGENT",
    "SCRIPT_FILENAME","SCRIPT_NAME","GATEWAY_INTERFACE",
];
foreach ($keys as $k) {
    $v = getenv($k);
    echo "$k=" . ($v === false ? "" : $v) . "\n";
}
