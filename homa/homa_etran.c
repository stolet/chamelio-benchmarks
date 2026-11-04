#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "homa_etran.h"
#include "../utils/core.h"

static int parse_cores(struct homa_etran_config *config, const char *list);
static int parse_number(const char *text, unsigned int *value);
static int parse_rate(const char *text, double *value);
static int parse_unsigned_list(const char *text, unsigned int *values,
    unsigned int ncores, unsigned int min, unsigned int max);
static int parse_rate_list(const char *text, double *values,
    unsigned int ncores);
static int parse_address(struct sockaddr_in *addr, char *ip, char *port);

int homa_etran_parse(struct homa_etran_config *config, int argc, char **argv,
    int client)
{
  unsigned int value;
  int first = client ? 5 : 3;
  int i;
  const char *cores = NULL;
  int rate_given = 0;
  int window_given = 0;
  int pending_given = 0;
  const char *msize = NULL;
  const char *mpending = NULL;
  const char *rate = NULL;

  memset(config, 0, sizeof(*config));
  config->duration = 30;
  config->core = -1;
  config->ncores = 1;

  if (argc < first ||
      parse_address(&config->local, argv[1], argv[2]) != 0)
    goto failed;

  if (client && parse_address(&config->remote, argv[3], argv[4]) != 0)
    goto failed;

  for (i = first; i < argc; i++)
  {
    if (client && strcmp(argv[i], "--validate") == 0)
    {
      config->validate = 1;
      continue;
    }

    if (i + 1 == argc)
      goto failed;

    if (strcmp(argv[i], "--cores") == 0)
    {
      cores = argv[++i];
      continue;
    }

    if (strcmp(argv[i], "--msize") == 0)
    {
      msize = argv[++i];
      continue;
    }

    if (client && strcmp(argv[i], "--rate-limit") == 0)
    {
      const char *mode = argv[++i];
      if (strcmp(mode, "rate") != 0 && strcmp(mode, "window") != 0)
        goto failed;
      config->window_mode = strcmp(mode, "window") == 0;
      continue;
    }

    if (client && strcmp(argv[i], "--window") == 0)
    {
      window_given = 1;
      mpending = argv[++i];
      continue;
    }

    if (client && strcmp(argv[i], "--rate") == 0)
    {
      rate_given = 1;
      rate = argv[++i];
      continue;
    }

    if (client && strcmp(argv[i], "--mpending") == 0)
    {
      pending_given = 1;
      mpending = argv[++i];
      continue;
    }

    if (parse_number(argv[i + 1], &value) != 0)
      goto failed;

    if (strcmp(argv[i], "--ncores") == 0)
    {
      if (value == 0 || value > HOMA_ETRAN_MAX_CORES)
        goto failed;
      config->ncores = value;
    }
    else if (client && strcmp(argv[i], "--duration") == 0 && value > 0)
      config->duration = value;
    else
      goto failed;

    i++;
  }

  if (client && ((config->window_mode &&
      (!window_given || rate_given || pending_given)) ||
      (!config->window_mode && window_given)))
    goto failed;

  /* RpcSocket caches the supplied port; it cannot use an ephemeral port.
   * eTran cannot transmit empty messages. */
  if (config->local.sin_port == 0)
  {
    fprintf(stderr, "eTran Homa requires a nonzero bind port\n");
    goto failed;
  }

  if (parse_unsigned_list(msize ? msize : "64", config->msize,
      config->ncores, 1, HOMA_ETRAN_MAX_MESSAGE) != 0 ||
      parse_unsigned_list(mpending ? mpending : "16", config->mpending,
      config->ncores, 1, HOMA_ETRAN_MAX_PENDING) != 0 ||
      parse_rate_list(rate ? rate : "10.0", config->rate,
      config->ncores) != 0)
    goto failed;

  unsigned int local_port = ntohs(config->local.sin_port);
  unsigned int remote_port = ntohs(config->remote.sin_port);
  int local_valid = local_port == 0 ||
      local_port + config->ncores - 1 <= UINT16_MAX;
  int remote_valid = !client || (remote_port != 0 &&
      remote_port + config->ncores - 1 <= UINT16_MAX);
  int server_valid = client || config->ncores == 1 || local_port != 0;

  if (!local_valid || !remote_valid || !server_valid)
    goto failed;

  if (parse_cores(config, cores) != 0)
    return -1;

  return 0;

failed:
  fprintf(stderr, "Usage: %s <bind_ip> <port>%s"
      " [--msize B[,B...]] [--ncores N] [--cores CPU,...]%s\n", argv[0],
      client ? " <rem_ip> <rem_port>" : "",
      client ? " [--duration S] [--rate-limit rate|window] [--window N] [--rate Mb/s[,Mb/s...]]"
      " [--mpending N[,N...]] [--validate]" : "");
  return -1;
}

static int parse_number(const char *text, unsigned int *value)
{
  unsigned long parsed;
  char *end;

  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno != 0 || *text == '\0' || *text == '-' || *end != '\0' ||
      parsed > UINT_MAX)
    return -1;

  *value = parsed;
  return 0;
}

static int parse_address(struct sockaddr_in *addr, char *ip, char *port)
{
  unsigned int value;

  if (parse_number(port, &value) != 0 || value > 65535)
    return -1;

  addr->sin_family = AF_INET;
  addr->sin_port = htons(value);
  return inet_pton(AF_INET, ip, &addr->sin_addr) == 1 ? 0 : -1;
}

static int parse_rate(const char *text, double *value)
{
  char *end;
  double parsed;

  errno = 0;
  parsed = strtod(text, &end);
  if (errno != 0 || *text == '\0' || *end != '\0')
    return -1;

  if (!isfinite(parsed) || parsed <= 0.0)
    return -1;

  *value = parsed;
  return 0;
}

static int parse_unsigned_list(const char *text, unsigned int *values,
    unsigned int ncores, unsigned int min, unsigned int max)
{
  unsigned int count = 0, i, value;
  char *copy = strdup(text);
  char *next, *token;

  if (copy == NULL)
    return -1;

  next = copy;
  while ((token = strsep(&next, ",")) != NULL)
  {
    if (count == ncores || parse_number(token, &value) != 0 ||
        value < min || value > max)
    {
      free(copy);
      return -1;
    }

    values[count++] = value;
  }
  free(copy);

  if (count == 1)
  {
    for (i = 1; i < ncores; i++)
      values[i] = values[0];
    return 0;
  }

  if (count != ncores)
  {
    fprintf(stderr, "Parameter list must contain one value or one value per worker\n");
    return -1;
  }

  return 0;
}

static int parse_rate_list(const char *text, double *values,
    unsigned int ncores)
{
  unsigned int count = 0, i;
  double value;
  char *copy = strdup(text);
  char *next, *token;

  if (copy == NULL)
    return -1;

  next = copy;
  while ((token = strsep(&next, ",")) != NULL)
  {
    if (count == ncores || parse_rate(token, &value) != 0)
    {
      free(copy);
      return -1;
    }

    values[count++] = value;
  }
  free(copy);

  if (count == 1)
  {
    for (i = 1; i < ncores; i++)
      values[i] = values[0];
    return 0;
  }

  if (count != ncores)
  {
    fprintf(stderr, "Parameter list must contain one value or one value per worker\n");
    return -1;
  }

  return 0;
}

static int parse_cores(struct homa_etran_config *config, const char *list)
{
  unsigned int count = 0, cpu, i;
  char *copy, *next, *token;

  for (i = 0; i < config->ncores; i++)
    config->cores[i] = -1;

  if (list == NULL)
    return 0;

  copy = strdup(list);
  if (copy == NULL)
    return -1;

  next = copy;
  while ((token = strsep(&next, ",")) != NULL)
  {
    if (count == config->ncores || parse_number(token, &cpu) != 0 ||
        cpu >= CPU_SETSIZE)
      break;

    config->cores[count++] = cpu;
  }
  free(copy);

  if (token != NULL || count != config->ncores)
  {
    fprintf(stderr, "--cores requires one CPU per worker\n");
    return -1;
  }

  return 0;
}
