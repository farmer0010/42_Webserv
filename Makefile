# ==============================================================================
# Target & Compiler Setup
# ==============================================================================
NAME        = webserv
CXX         = c++
CXXFLAGS    = -Wall -Wextra -Werror -std=c++98
INCLUDES    = -I includes

# ==============================================================================
# Directories
# ==============================================================================
SRCS_DIR    = srcs
OBJS_DIR    = objs

# ==============================================================================
# Source Files (새로운 cpp 파일이 생길 때마다 여기에 추가해주세요!)
# ==============================================================================
SRCS_FILES  = main.cpp \
              core/ServerSocket.cpp \
              core/ServerManager.cpp \
              core/ClientSocket.cpp
# 나중에 추가될 파일들 예시:
#             http/Request.cpp \
#             http/Response.cpp \
#             config/ConfigParser.cpp

# SRCS_FILES 앞부분에 srcs/ 폴더 경로를 붙임
SRCS        = $(addprefix $(SRCS_DIR)/, $(SRCS_FILES))

# SRCS_FILES 확장자를 .cpp에서 .o로 바꾸고 objs/ 폴더 경로를 붙임
OBJS        = $(addprefix $(OBJS_DIR)/, $(SRCS_FILES:.cpp=.o))

# ==============================================================================
# Rules
# ==============================================================================

# 기본 타겟
all: $(NAME)

# 실행 파일 생성
$(NAME): $(OBJS)
	@echo "Linking $(NAME)..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(NAME) $(OBJS)
	@echo "✨ Webserv compiled successfully! ✨"

# 각 .cpp 파일을 .o 파일로 컴파일
$(OBJS_DIR)/%.o: $(SRCS_DIR)/%.cpp
	@# objs/ 폴더 내부에 core, http 같은 하위 폴더가 없으면 에러가 나므로 미리 만들어줍니다.
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 오브젝트 파일 삭제
clean:
	@echo "Cleaning object files..."
	@rm -rf $(OBJS_DIR)

# 실행 파일까지 모두 삭제
fclean: clean
	@echo "Cleaning executable..."
	@rm -f $(NAME)

# 전체 다시 빌드
re: fclean all

.PHONY: all clean fclean re
