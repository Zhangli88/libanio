/*
  This file is part of libanio (https://github.com/yoones/libanio).

  libanio is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  libanio is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with libanio.  If not, see <http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "libanio.h"

static void		_thread_cancellation_point(t_anio *server)
{
  if ((int)server->thread_pool.max_workers < server->thread_pool.workers.size)
    {
      x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
      pthread_exit((void *)EXIT_SUCCESS);
    }
}

static int		_poll_and_handle_available_event(t_anio *server)
{
  struct epoll_event	*jobs = server->thread_pool.jobs;
  struct epoll_event	my_job;

  server->thread_pool.busy_workers++;
  server->thread_pool.remaining_jobs--;
  memcpy(&my_job, jobs + server->thread_pool.remaining_jobs, sizeof(struct epoll_event));
  if (x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex))
    return (-1);
  if (libanio_handle_event(server, &my_job) == -1)
    {
      /* handle handler's error */
    }
  if (x_pthread_mutex_lock(&server->thread_pool.jobs_mutex))
    return (-1);
  server->thread_pool.busy_workers--;
  return (0);
}

static int		_wait_for_events_loop(t_anio *server)
{
  while (1)
    {
      while (1)
	{
	  if (x_pthread_cond_wait(&server->thread_pool.jobs_condvar, &server->thread_pool.jobs_mutex))
	    return (-1);
	  _thread_cancellation_point(server);
	  if (server->thread_pool.remaining_jobs == 0)
	    continue ;
	  else
	    break ;
	}
      if (_poll_and_handle_available_event(server))
	return (-1);
    }
}

static void		*_worker_main(void *arg)
{
  t_anio		*server = (t_anio *)arg;
  int			ret;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  if (x_pthread_mutex_lock(&server->thread_pool.jobs_mutex))
    pthread_exit((void *)EXIT_FAILURE);
  ret = _wait_for_events_loop(server);
  ret |= x_pthread_mutex_unlock(&server->thread_pool.jobs_mutex);
  pthread_exit(ret == 0 ? (void *)EXIT_SUCCESS : (void *)EXIT_FAILURE);
}

int		libanio_create_workers(t_anio *server)
{
  size_t	i;
  pthread_t	*worker;
  int		ret;

  if (server->thread_pool.max_workers == 0
      || server->thread_pool.workers.size > 0)
    return (-1);
  for (i = 0; i < server->thread_pool.max_workers; i++)
    {
      if (!(worker = malloc(sizeof(pthread_t))))
	{
	  print_err(errno);
	  list_clear(&server->thread_pool.workers);
	  return (-1);
	}
      if ((ret = pthread_create(worker,
				NULL,
				&_worker_main,
				(void *)server)) != 0)
	{
	  print_err(ret);
	  print_err(ret);
	  free(worker);
	  list_clear(&server->thread_pool.workers);
	  return (-1);
	}
      if (list_push_back(&server->thread_pool.workers, worker) == -1)
	{
	  pthread_cancel(*worker);
	  free(worker);
	  list_clear(&server->thread_pool.workers);
	  return (-1);
	}
    }
  return (0);
}
