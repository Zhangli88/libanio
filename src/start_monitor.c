#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "libanio.h"

#define EPOLL_MAX_EVENTS 1024
/* todo: if getrlimit(NOFILE) > 0, use this value instead of EPOLL_MAX_EVENTS  */

static int	_watch_server_fd(t_anio *server)
{
  server->fdesc.event.data.fd = server->fdesc.fd;
  server->fdesc.event.events = EPOLLIN; /* todo: use EPOLLRDHUP to detected closed socket */
  if (epoll_ctl(server->thread_pool.epoll_fd, EPOLL_CTL_ADD, server->fdesc.fd, &server->fdesc.event) == -1)
    {
      perror("epoll_ctl(server)");
      return (-1);
    }
  return (0);
}

static void		*_start_monitor(void *arg)
{
  t_anio		*server = arg;
  int			err_flag;
  int			ret;

  if ((server->thread_pool.epoll_fd = epoll_create1(0)) == -1
      || !(server->thread_pool.jobs = calloc(EPOLL_MAX_EVENTS, sizeof(struct epoll_event)))
      || libanio_create_workers(server) == -1)
    {
      perror(NULL);
      close(server->thread_pool.epoll_fd);
      server->thread_pool.epoll_fd = -1;
      pthread_exit((void *)EXIT_FAILURE);
    }
  if (_watch_server_fd(server) == -1)
    {
      close(server->thread_pool.epoll_fd);
      server->thread_pool.epoll_fd = -1;
      libanio_destroy_workers(server);
      pthread_exit((void *)EXIT_FAILURE);
    }
  err_flag = 0;
#define BREAK_ON_ERR(ret, err_flag) if (ret != 0) { err_flag = 1; break ; }
  while (!err_flag)
    {
      /* prepare what epoll needs and wait for events */
      printf("DEBUG: monitor mutex_lock (part 1) ??\n");
      ret = x_pthread_mutex_lock(&server->thread_pool.jobs_mutex);
      printf("DEBUG: monitor mutex_lock (part 1) OK (%d)\n", ret);
      BREAK_ON_ERR(ret, err_flag);
      printf("DEBUG: monitor epoll_wait\n");
      if ((server->thread_pool.remaining_jobs = epoll_wait(server->thread_pool.epoll_fd, server->thread_pool.jobs, EPOLL_MAX_EVENTS, -1)) == -1)
	{
	  server->thread_pool.remaining_jobs = 0;
	  perror(NULL);
	  break ;
	}
      printf("DEBUG: monitor mutex_unlock (part 1)\n");
      ret = x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
      BREAK_ON_ERR(ret, err_flag);
      /* let workers take care of epoll events */
      while (!err_flag)
	{
	  printf("DEBUG: monitor mutex_lock (part 2)\n");
	  ret = x_pthread_mutex_lock(&server->thread_pool.jobs_mutex);
	  BREAK_ON_ERR(ret, err_flag);
	  printf("DEBUG: monitor has %d jobs\n", server->thread_pool.remaining_jobs);
	  if (server->thread_pool.remaining_jobs > 0)
	    {
	      printf("DEBUG: monitor cond_broadcast\n");
	      ret = x_pthread_cond_broadcast(&server->thread_pool.jobs_condvar);
	      BREAK_ON_ERR(ret, err_flag);
	    }
	  else
	    {
	      printf("DEBUG: monitor mutex_unlock\n");
	      ret = x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
	      BREAK_ON_ERR(ret, err_flag);
	      break ;
	    }
	  printf("DEBUG: monitor mutex_unlock (part 2)\n");
	  ret = x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
	  BREAK_ON_ERR(ret, err_flag);
	}
    }
#undef BREAK_ON_ERR
  (void)libanio_destroy_workers(server);
  close(server->thread_pool.epoll_fd);
  server->thread_pool.epoll_fd = -1;
  free(server->thread_pool.jobs);
  server->thread_pool.jobs = NULL;
  server->thread_pool.remaining_jobs = 0;
  (void)x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
  pthread_exit((void *)EXIT_FAILURE);
  return (NULL);
}

int		libanio_start_monitor(t_anio *server)
{
  if (x_pthread_mutex_trylock(&server->monitoring_thread_mutex) != 0) /* could use a pthread_tryjoin instead? */
    return (-1);
  if (x_pthread_create(&server->monitoring_thread, NULL, &_start_monitor, (void *)server) != 0)
    return (-1);
  return (0);
}
