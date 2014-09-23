/*
Copyright © Léo Flaventin Hauchecorne <hl037.prog@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>. 1
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>
#include <setjmp.h>
#include <dirent.h>
#include <glob.h>
#include <stdarg.h>
#include <libconfig.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#define __USE_MISC 1
#include <sys/syslog.h>

#define TARGET "ix-cool"

#define min(a,b) ( a<b ? a:b )
#define max(b,a) ( a<b ? a:b )

jmp_buf loop_env;

char SIGTERM_received = 0;

typedef struct Conf
{
   int check_period;
   int temp_critic;
   int temp_high;
   int temp_max_ok;
   int temp_min_ok;
   double cpu_min;
   double cpu_max;
   double cpu_inc_seil;
   double dec_high_critic;
   double dec_ok_high;
   double cpu_inc;
} Conf;
Conf conf;


typedef enum State
{
   PANIC,
   CRITIC,
   HIGH,
   OK,
   LOW
} State;
State state;


typedef struct CPU_Stat
{
   int active;
   int idle;
} CPU_Stat;


size_t readfile(char * ptr, size_t size, size_t nmemb, char * path)
{
   FILE * f = fopen(path, "r");
   size_t r;
   if(f == NULL)
   {
      return 0;
   }
   r = fread(ptr, size, nmemb, f);
   fclose(f);
   return r;
}

size_t writefile(const void *ptr, size_t size, size_t nmemb, char * path)
{
   FILE * f = fopen(path, "w");
   size_t r;
   if(f == NULL)
   {
      return 0;
   }
   r = fwrite(ptr, size, nmemb, f);
   fclose(f);
   return r;
}


void update();

void fatal_error(int ret, char * msg, ...)
{
   va_list args;
   va_start (args, msg);
   vsyslog(LOG_CRIT, msg, args);
   exit(ret);
}

void crit_error(char * msg, ...)
{
   va_list args;
   va_start(args, msg);
   vsyslog(LOG_ERR, msg, args);
   longjmp(loop_env, 1);
}

void error(char * msg, ...)
{
   va_list args;
   va_start(args, msg);
   vsyslog(LOG_ERR, msg, args);
}

void info(char * msg, ...)
{
   va_list args;
   va_start(args, msg);
   vsyslog(LOG_INFO, msg, args);
}

static void mkdir_r(const char *dir, mode_t m) {
   char tmp[256];
   char *p = NULL;
   size_t len;

   snprintf(tmp, sizeof(tmp),"%s",dir);
   len = strlen(tmp);
   if(tmp[len - 1] == '/')
      tmp[len - 1] = 0;
   for(p = tmp + 1; *p; p++)
      if(*p == '/') {
         *p = 0;
         mkdir(tmp, m);
         *p = '/';
      }
   mkdir(tmp, m);
}

void set_default_conf()
{
   conf.check_period=2;
   conf.temp_critic=76000;
   conf.temp_high=72000;
   conf.temp_max_ok=68000;
   conf.temp_min_ok=60000;
   conf.cpu_min=15;
   conf.cpu_max=90;
   conf.cpu_inc_seil=80;
   conf.dec_high_critic=20;
   conf.dec_ok_high=5;
   conf.cpu_inc=10;;
}

void read_conf(FILE * f)
{

   char err = 1;
   config_t c;
   config_init(&c);
   config_set_auto_convert(&c, 1);
   if(CONFIG_TRUE == config_read(&c, f))
   {
      config_lookup_int(&c, "check_period", &(conf.check_period));
      config_lookup_int(&c, "temp_critic", &(conf.temp_critic));
      config_lookup_int(&c, "temp_high", &(conf.temp_high));
      config_lookup_int(&c, "temp_max_ok", &(conf.temp_max_ok));
      config_lookup_int(&c, "temp_min_ok", &(conf.temp_min_ok));
      config_lookup_float(&c, "cpu_min", &(conf.cpu_min));
      config_lookup_float(&c, "cpu_max", &(conf.cpu_max));
      config_lookup_float(&c, "cpu_inc_seil", &(conf.cpu_inc_seil));
      config_lookup_float(&c, "dec_high_critic", &(conf.dec_high_critic));
      config_lookup_float(&c, "dec_ok_high", &(conf.dec_ok_high));
      config_lookup_float(&c, "cpu_inc", &(conf.cpu_inc));
      err = 0;
   }
   
   if(err)
   {
      error("Configuration error at %s(%d) : %s", config_error_file(&c), config_error_line(&c), config_error_text(&c));
   }
   
   config_destroy(&c);
}

void write_default_conf(FILE * f)
{
   char c[] =
      "# Period between two temperature checks\n"
      "check_period=2\n\n"
      
      "# Critical temperature limit ( 1000 = 1°C ) above which the CPU max p-state is set to the minimum\n"
      "temp_critic=76000\n\n"
      
      "# \"High\" temperature ( 1000 = 1°C ) above which the CPU max p-state is decreased by dec_high_critic\n"
      "temp_high=72000\n\n"
      
      "# Maximum normal temperature ( 1000 = 1°C ) above which the CPU max p-state is decreased by dec_ok_high.\n"
      "temp_max_ok=68000\n\n"
      
      "# Minimum normal temperature ( 1000 = 1°C ) under wich the CPU max p-state can be increased, bu not above.\n"
      "temp_min_ok=60000\n\n"
      
      "# Minimum p-state pourcentage.\n"
      "cpu_min=15\n\n"
      
      "# Maximum p-state pourcentage.\n"
      "cpu_max=100\n\n"
      
      "# Minimum CPU-usage to increase max p-state if temperature is under temp_min_ok.\n"
      "cpu_inc_seil=80\n\n"
      
      "# Decrement step if Core temperature is between temp_high and temp_critic.\n"
      "dec_high_critic=20\n\n"
      
      "# Decrement step if Core temperature is between temp_max_ok and temp_high.\n"
      "dec_ok_high=5\n\n"
      
      "# Increment step if Core temperature is under temp_min_ok and CPU-usage is above cpu_inc_seil.\n"
      "cpu_inc=10\n\n";
   fputs(c, f);
}


int get_cpu_temp();
double get_cpu_usage();
void set_cpu_max(double pct);
void set_cpu_min(double pct);
double get_cpu_max();

void onSIGHUP(int sig)
{
   FILE * f = fopen("/etc/conf.d/"TARGET".conf", "r");
   if(NULL == f)
   {
      mkdir_r("/etc/conf.d", 0644);
      f = fopen("/etc/conf.d/"TARGET".conf", "w");
      if(NULL != f)
      {
         write_default_conf(f);
         fclose(f);
         info("Configuration file not found, default created");
      }
      else
      {
         error("Cannot write default configuration");
      }
   }
   else
   {
      read_conf(f);
      fclose(f);
      info("Configuration reloaded");
   }
   set_cpu_min(conf.cpu_min);
   set_cpu_max(conf.cpu_max);
}

void onSIGTERM(int sig)
{
   SIGTERM_received = 1;
   info("Sigterm received, stopping...");
}
   

int main(int argc, char* argv[])
{
   FILE * f;
   int r;

   set_default_conf();

   openlog(NULL, LOG_PID, LOG_USER);

   signal(SIGTERM, &onSIGTERM);
   signal(SIGHUP, &onSIGHUP);

   
   f = fopen("/etc/conf.d/"TARGET".conf", "r");
   if(NULL == f)
   {
      mkdir_r("/etc/conf.d", 0644);
      f = fopen("/etc/conf.d/"TARGET".conf", "w");
      if(NULL != f)
      {
         write_default_conf(f);
         fclose(f);
      }
      else
      {
         error("Cannot write default configuration");
      }
   }
   else
   {
      read_conf(f);
      fclose(f);
   }
   set_cpu_min(conf.cpu_min);
   set_cpu_max(conf.cpu_max);

   r = setjmp(loop_env);
   while(!SIGTERM_received)
   {
      update();
      sleep(conf.check_period);
   }
   return 0;
}

int get_cpu_temp()
{
   char base[1024] = "/sys/class/hwmon/";
   char filename[1024];
   char monpath[1024];
   char line[256];
   glob_t r;
   DIR * d = opendir(base);
   struct dirent * ent;
   int i;
   int t_max = 0, t;
   while(NULL != (ent = readdir(d)) )
   {
      snprintf(filename, sizeof(filename), "%s%s/name", base, ent->d_name);
      if(0 == readfile(line, sizeof(char), sizeof(line)/sizeof(char), filename))
      {
         continue;
      }
      if(0 != strncmp("coretemp", line, 8))
      {
         continue;
      }
      snprintf(filename, sizeof(monpath), "%s%s/temp*input", base, ent->d_name);
      if(0 != glob(filename, GLOB_NOSORT, NULL, &r))
      {
         continue;
      }
      for(i = 0 ; i < r.gl_pathc ; ++i)
      {
         if(0 == readfile(line, sizeof(char), sizeof(line)/sizeof(char), r.gl_pathv[i]))
         {
            continue;
         }
         t = atoi(line);
         if(t_max < t)
         {
            t_max = t;
         }
      }
   }
   closedir(d);
   return t_max;
}

void set_cpu_max(double pct)
{
   char line[10];
   FILE * cpu_max = fopen("/sys/devices/system/cpu/intel_pstate/max_perf_pct", "w");
   if(cpu_max == NULL)
   {
      error("Cannot write intel p-state max allowed (cannot write /sys/devices/system/cpu/intel_pstate/max_perf_pct)");
      exit(1);
      return;
   }
   snprintf(line, 9, "%d", (int) pct);
   fputs(line, cpu_max);
   fclose(cpu_max);
   info("CPU p-state max set to %d", (int) conf.cpu_max);
}

void set_cpu_min(double pct)
{
   char line[10];
   FILE * cpu_max = fopen("/sys/devices/system/cpu/intel_pstate/min_perf_pct", "w");
   if(cpu_max == NULL)
   {
      error("Cannot write intel p-state max allowed (cannot write /sys/devices/system/cpu/intel_pstate/min_perf_pct)");
      exit(1);
      return;
   }
   snprintf(line, 9, "%d", (int) pct);
   fputs(line, cpu_max);
   fclose(cpu_max);
   info("CPU p-state min set to %d", (int) conf.cpu_min);
}

double get_cpu_max()
{
   char line[10];
   FILE * cpu_max = fopen("/sys/devices/system/cpu/intel_pstate/max_perf_pct", "r");
   if(cpu_max == NULL)
   {
      error("Cannot retrieve intel p-state max allowed (cannot read /sys/devices/system/cpu/intel_pstate/max_perf_pct)");
      exit(1);
      return 0;
   }
   if(NULL == fgets(line, 9, cpu_max))
   {
      error("No value in /sys/devices/system/cpu/intel_pstate/max_perf_pct");
      exit(1);
      return 0;
   }
   fclose(cpu_max);
   return atoi(line);
}
   
   

double get_cpu_usage()
{
   const char tok[] = " \t";
   static CPU_Stat prev_stat = {0,0};
   CPU_Stat cur_stat = {0,0};
   char * saveptr;
   char line[256];
   FILE * stat = fopen("/proc/stat", "r");
   double r;
   if(stat == NULL)
   {
      error("Cannot get cpu usage (cannot read /proc/stat)");
      exit(1);
      return 0;
   }
   while(NULL != fgets(line, 255, stat))
   {
      int i=0;
      char * field = strtok_r(line, tok, &saveptr);
      if(field == NULL 
            || field[i++] != 'c' 
            || field[i++] != 'p' 
            || field[i++] != 'u' 
            || field[i++] != '\0' )
      {
         continue;
      }
      cur_stat.active = atoi(strtok_r(NULL, tok, &saveptr))
                        + atoi(strtok_r(NULL, tok, &saveptr)) 
                        + atoi(strtok_r(NULL, tok, &saveptr));
      while(NULL != (field = strtok_r(NULL, tok, &saveptr)))
      {
         cur_stat.idle += atoi(field);
      }
      break;
   }
   fclose(stat);
   if(prev_stat.active == 0)
   {
      if(cur_stat.active != 0)
      {
         prev_stat = cur_stat;
         sleep(1);
         return get_cpu_usage();
      }
      error("Cannot read cpu usage");
      exit(1);
      return 0;
   }
   r = ((double) (100 * (cur_stat.active - prev_stat.active))) / ((double) (cur_stat.idle - prev_stat.idle));
   prev_stat = cur_stat;
   return r;
}

void update()
{
   int t = get_cpu_temp();
   if(t > conf.temp_critic)
   {
      set_cpu_max(conf.cpu_min);
      state = PANIC;
      info("Critic temperature reached, CPU set to minimum", conf.cpu_min);
      info("CPU max pourcentage set to %d", conf.cpu_min);
   }
   else if(t > conf.temp_high)
   {
      if(state != PANIC)
      {
         double cpu_max = get_cpu_max();
         info("HIGH temperature reached, CPU decreasing by", conf.dec_high_critic);
         cpu_max = max(cpu_max - conf.dec_high_critic, conf.cpu_min);
         set_cpu_max(cpu_max);
         state = CRITIC;
      }
   }
   else if(t > conf.temp_max_ok)
   {
      if(state != PANIC)
      {
         double cpu_max = get_cpu_max();
         info("MAX OK temperature reached, CPU decreasing by", conf.dec_ok_high);
         cpu_max = max(cpu_max - conf.dec_ok_high, conf.cpu_min);
         set_cpu_max(cpu_max);
         state = HIGH;
      }
   }
   else if(t > conf.temp_min_ok)
   {
      if(state == PANIC)
      {
         state = OK;
      }
      info("Temperature is ok, nothing to do.");
   }
   else
   {
      double cpu_usage = get_cpu_usage();
      if(state == PANIC)
      {
         state = OK;
      }
      if(cpu_usage > conf.cpu_inc_seil)
      {
         double cpu_max = get_cpu_max();
         info("CPU usage reached %f ( < %f ) and temperature is ok so increase max by %f", cpu_usage, conf.cpu_inc_seil, conf.cpu_inc);
         cpu_max = min(conf.cpu_max, cpu_max + conf.cpu_inc);
         set_cpu_max(cpu_max);
      }
   }
}

