#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include <map>

class Location {
	public:
		std::string                 path;           // 매칭할 URI 경로 (예: "/kapouet")
		std::string                 root;           // 변환될 실제 폴더 경로 (예: "/tmp/www")

		std::vector<std::string>    allowedMethods; // 허용 메서드 (예: "GET", "POST")
		std::pair<int, std::string> redirection;    // 리다이렉트 (상태코드, 이동할 URL)

		bool                        autoindex;      // 디렉토리 목록 출력 여부 (true/false)
		std::string                 index;          // 기본 파일 (예: "index.html")

		bool                        uploadEnabled;  // 업로드 허용 여부
		std::string                 uploadStore;    // 업로드된 파일이 저장될 물리적 경로

		std::string                 cgiExtension;   // CGI 처리를 할 확장자 (예: ".php")
		std::string                 cgiPath;        // CGI 실행 파일의 절대/상대 경로
};

// [Server Block] 하나의 가상 서버에 대한 규칙
class ServerBlock {
	public:
		std::string                 host;              // 인터페이스 IP (예: "0.0.0.0" 또는 "127.0.0.1")
		int                         port;              // Listen 포트 번호 (예: 8080)
		std::string                 serverName;        // 도메인 이름 (예: "localhost", "example.com")

		size_t                      clientMaxBodySize; // 바디 제한 크기 (바이트 단위)
		std::map<int, std::string>  errorPages;        // 에러 코드별 커스텀 페이지 맵핑

		std::vector<Location>       locations;         // 이 서버가 품고 있는 세부 경로 규칙들
};

// [전체 Config] 프로그램 전체의 설정
class Config {
	private:
		std::vector<ServerBlock> _servers;

	public:
		// 파서가 채워넣을 때 사용하는 함수
		void addServerBlock(const ServerBlock& server);

		// 네트워크 파트(질문자님)나 HTTP 파트가 서버 설정들을 조회할 때 사용하는 함수
		const std::vector<ServerBlock>& getServerBlocks() const;
};

#endif
