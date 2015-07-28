##
##
##
##

CC		=	gcc
RM		=	rm -rf
AR		=	ar
DOXYGEN		=	doxygen

INCLUDES	=	-Iinclude -I.
CFLAGS		+=	-W -Wall -Wextra $(INCLUDES) -ggdb
LDFLAGS		=	-lpthread

## Library
NAME		=	libanio.a

_SRCS		=	utils/list.c				\
			utils/pthread_helpers.c			\
			utils/epoll_helpers.c			\
			init/init.c				\
			init/set_callback_on_accept.c		\
			init/set_callback_on_read.c		\
			init/set_callback_on_eof.c		\
			init/set_callback_on_error.c		\
			init/set_callbacks.c			\
			init/set_max_clients.c			\
			init/set_thread_pool_size.c		\
			init/free.c				\
			monitor/start_monitor.c			\
			monitor/stop_monitor.c			\
			monitor/is_server_alive.c		\
			workers/create_workers.c		\
			workers/handle_event.c			\
			workers/destroy_workers.c		\
			fdesc/fdesc_init.c			\
			fdesc/fdesc_close.c			\
			fdesc/has_client.c			\
			fdesc/add_client.c			\
			fdesc/remove_client.c			\
			fdesc/get_client.c
SRCS		=	$(addprefix src/, $(_SRCS))
OBJS		=	$(SRCS:.c=.o)

## Debug binary
NAME_DEBUG	=	poc
SRCS_DEBUG	=	$(NAME)			\
			debug/main.c
OBJS_DEBUG	=	$(SRCS_DEBUG:.c=.o)

## Rules

all		:	$(NAME) $(NAME_DEBUG)

$(NAME)		:	$(OBJS)
			$(AR) rcs $(NAME) $(OBJS)

$(NAME_DEBUG)	:	$(NAME) $(OBJS_DEBUG)
			$(CC) -o $(NAME_DEBUG) $(OBJS_DEBUG) $(NAME) $(LDFLAGS)

clean		:
			$(RM) $(NAME)
			$(RM) $(NAME_DEBUG)

fclean		:	clean
			$(RM) $(OBJS)
			$(RM) $(OBJS_DEBUG)

re		:	fclean all

.PHONY		:	all clean fclean re
