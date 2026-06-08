NAME        = webserv
CXX         = c++
CXXFLAGS    = -Wall -Wextra -Werror -std=c++98
INCLUDES    = -I includes

SRCS_DIR    = srcs
OBJS_DIR    = objs

SRCS_FILES  = main.cpp \
              core/ServerSocket.cpp \
              core/ServerManager.cpp \
              core/ClientSocket.cpp \
              config/ConfigParser.cpp \
              config/ServerBlock.cpp \
              http/HttpRequest.cpp \
              http/HttpResponse.cpp \
              http/RequestHandler.cpp \
              http/Cgi.cpp

SRCS        = $(addprefix $(SRCS_DIR)/, $(SRCS_FILES))
OBJS        = $(addprefix $(OBJS_DIR)/, $(SRCS_FILES:.cpp=.o))

all: $(NAME)

$(NAME): $(OBJS)
	@echo "Linking $(NAME)..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(NAME) $(OBJS)
	@echo "✨ Webserv compiled successfully! ✨"

$(OBJS_DIR)/%.o: $(SRCS_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@echo "Cleaning object files..."
	@rm -rf $(OBJS_DIR)

fclean: clean
	@echo "Cleaning executable..."
	@rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re