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
      print_err(errno);
      return (-1);
    }
  return (0);
}

static void		*_monitor_main(void *arg)
{
  t_anio		*server = arg;
  int			err_flag;
  int			ret;

  if ((server->thread_pool.epoll_fd = epoll_create1(0)) == -1
      || !(server->thread_pool.jobs = calloc(EPOLL_MAX_EVENTS, sizeof(struct epoll_event)))
      || libanio_create_workers(server) == -1)
    {
      print_err(errno);
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
      ret = x_pthread_mutex_lock(&server->thread_pool.jobs_mutex);
      BREAK_ON_ERR(ret, err_flag);
      if ((server->thread_pool.remaining_jobs = epoll_wait(server->thread_pool.epoll_fd, server->thread_pool.jobs, EPOLL_MAX_EVENTS, -1)) == -1)
	{
	  server->thread_pool.remaining_jobs = 0;
	  print_err(errno);
	  break ;
	}
      ret = x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
      BREAK_ON_ERR(ret, err_flag);
      /* let workers take care of epoll events */
      while (!err_flag)
	{
	  DEBUG(CYAN, "try to lock mutex...");
	  ret = x_pthread_mutex_lock(&server->thread_pool.jobs_mutex);
	  BREAK_ON_ERR(ret, err_flag);
	  DEBUG(CYAN, "try to lock mutex SUCCESS");
	  DEBUG(YELLOW, "busy workers: %d", server->thread_pool.busy_workers);
	  if (server->thread_pool.remaining_jobs > 0)
	    {
	      if (server->thread_pool.workers.size == 0)
		{
		  print_custom_err("ERROR: no worker available!!!");
		  abort();	/* todo: manage this case. can should the monitor run if there's no worker?? */
		}
	      ret = x_pthread_cond_broadcast(&server->thread_pool.jobs_condvar);
	      BREAK_ON_ERR(ret, err_flag);
	    }
	  else if (server->thread_pool.busy_workers > 0)
	    {
	      sleep(1);
	    }
	  else
	    {
	      ret = x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
	      BREAK_ON_ERR(ret, err_flag);
	      break ;
	    }
	  ret = x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
	  BREAK_ON_ERR(ret, err_flag);
	}
    }
#undef BREAK_ON_ERR
  DEBUG(RED, "DEBUG: monitor exits loop");
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
  if (x_pthread_create(&server->monitoring_thread, NULL, &_monitor_main, (void *)server) != 0)
    return (-1);
  return (0);
}
