/*=============================================================================
 Copyright (C) 2010 WebOS Internals <support@webos-internals.org>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 =============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "luna_service.h"
#include "luna_methods.h"

#define ALLOWED_CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"

//
// We use static buffers instead of continually allocating and deallocating stuff,
// since we're a long-running service, and do not want to leak anything.
//
static char line[MAXLINLEN];
static char filename[MAXLINLEN];
static char directory[MAXLINLEN];
static char buffer[MAXBUFLEN];
static char esc_buffer[MAXBUFLEN];
static char run_command_buffer[MAXBUFLEN];
static char errorText[MAXLINLEN];

static char *cpufreqdir = "/sys/devices/system/cpu/cpu0/cpufreq";

//
// Escape a string so that it can be used directly in a JSON response.
// In general, this means escaping quotes, backslashes and control chars.
// It uses the static esc_buffer, which must be twice as large as the
// largest string this routine can handle.
//
static char *json_escape_str(char *str)
{
  const char *json_hex_chars = "0123456789abcdef";

  // Initialise the output buffer
  strcpy(esc_buffer, "");

  // Check the constraints on the input string
  if (strlen(str) > MAXBUFLEN) return (char *)esc_buffer;

  // Initialise the pointers used to step through the input and output.
  char *resultsPt = (char *)esc_buffer;
  int pos = 0, start_offset = 0;

  // Traverse the input, copying to the output in the largest chunks
  // possible, escaping characters as we go.
  unsigned char c;
  do {
    c = str[pos];
    switch (c) {
    case '\0':
      // Terminate the copying
      break;
    case '\b':
    case '\n':
    case '\r':
    case '\t':
    case '"':
    case '\\': {
      // Copy the chunk before the character which must be escaped
      if (pos - start_offset > 0) {
	memcpy(resultsPt, str + start_offset, pos - start_offset);
	resultsPt += pos - start_offset;
      };
      
      // Escape the character
      if      (c == '\b') {memcpy(resultsPt, "\\b",  2); resultsPt += 2;} 
      else if (c == '\n') {memcpy(resultsPt, "\\n",  2); resultsPt += 2;} 
      else if (c == '\r') {memcpy(resultsPt, "\\r",  2); resultsPt += 2;} 
      else if (c == '\t') {memcpy(resultsPt, "\\t",  2); resultsPt += 2;} 
      else if (c == '"')  {memcpy(resultsPt, "\\\"", 2); resultsPt += 2;} 
      else if (c == '\\') {memcpy(resultsPt, "\\\\", 2); resultsPt += 2;} 

      // Reset the start of the next chunk
      start_offset = ++pos;
      break;
    }

    default:
      
      // Check for "special" characters
      if ((c < ' ') || (c > 127)) {

	// Copy the chunk before the character which must be escaped
	if (pos - start_offset > 0) {
	  memcpy(resultsPt, str + start_offset, pos - start_offset);
	  resultsPt += pos - start_offset;
	}

	// Insert a normalised representation
	sprintf(resultsPt, "\\u00%c%c",
		json_hex_chars[c >> 4],
		json_hex_chars[c & 0xf]);

	// Reset the start of the next chunk
	start_offset = ++pos;
      }
      else {
	// Just move along the source string, without copying
	pos++;
      }
    }
  } while (c);

  // Copy the final chunk, if required
  if (pos - start_offset > 0) {
    memcpy(resultsPt, str + start_offset, pos - start_offset);
    resultsPt += pos - start_offset;
  } 

  // Terminate the output buffer ...
  memcpy(resultsPt, "\0", 1);

  // and return a pointer to it.
  return (char *)esc_buffer;
}

//
// A dummy method, useful for unimplemented functions or as a status function.
// Called directly from webOS, and returns directly to webOS.
//
bool dummy_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  if (!LSMessageReply(lshandle, message, "{\"returnValue\": true}", &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Run a shell command, and return the output in-line in a buffer for returning to webOS.
// If lshandle, message and subscriber are defined, then also send back status messages.
// The global run_command_buffer must be initialised before calling this function.
// The return value says whether the command executed successfully or not.
//
static bool run_command(char *command, bool escape) {
  LSError lserror;
  LSErrorInit(&lserror);

  // Local buffers to store the current and previous lines.
  char line[MAXLINLEN];

  // fprintf(stderr, "Running command %s\n", command);

  // run_command_buffer is assumed to be initialised, ready for strcat to append.

  // Is this the first line of output?
  bool first = true;

  bool array = false;

  // Start execution of the command, and read the output.
  FILE *fp = popen(command, "r");

  // Return immediately if we cannot even start the command.
  if (!fp) {
    return false;
  }

  // Loop through the output lines
  while (fgets(line, sizeof line, fp)) {

    // Chomp the newline
    char *nl = strchr(line,'\n'); if (nl) *nl = 0;

    // Add formatting breaks between lines
    if (first) {
      if (run_command_buffer[strlen(run_command_buffer)-1] == '[') {
	array = true;
      }
      first = false;
    }
    else {
      if (array) {
	strcat(run_command_buffer, ", ");
      }
      else {
	strcat(run_command_buffer, "<br>");
      }
    }
    
    // Append the unfiltered output to the run_command_buffer.
    if (escape) {
      if (array) {
	strcat(run_command_buffer, "\"");
      }
      strcat(run_command_buffer, json_escape_str(line));
      if (array) {
	strcat(run_command_buffer, "\"");
      }
    }
    else {
      strcat(run_command_buffer, line);
    }
  }
  
  // Check the close status of the process
  if (pclose(fp)) {
    return false;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  // %%% We need a way to distinguish command failures from LSMessage failures %%%
  // %%% This may need to be true if we just want to ignore LSMessage failures %%%
  return false;
}

//
// Send a standard format command failure message back to webOS.
// The command will be escaped.  The output argument should be a JSON array and is not escaped.
// The additional text  will not be escaped.
// The return value is from the LSMessageReply call, not related to the command execution.
//
static bool report_command_failure(LSHandle* lshandle, LSMessage *message, char *command, char *stdErrText, char *additional) {
  LSError lserror;
  LSErrorInit(&lserror);

  // Include the command that was executed, in escaped form.
  snprintf(buffer, MAXBUFLEN,
	   "{\"errorText\": \"Unable to run command: %s\"",
	   json_escape_str(command));

  // Include any stderr fields from the command.
  if (stdErrText) {
    strcat(buffer, ", \"stdErr\": ");
    strcat(buffer, stdErrText);
  }

  // Report that an error occurred.
  strcat(buffer, ", \"returnValue\": false, \"errorCode\": -1");

  // Add any additional JSON fields.
  if (additional) {
    strcat(buffer, ", ");
    strcat(buffer, additional);
  }

  // Terminate the JSON reply message ...
  strcat(buffer, "}");

  // fprintf(stderr, "Message is %s\n", buffer);

  // and send it.
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Run a simple shell command, and return the output to webOS.
//
static bool simple_command(LSHandle* lshandle, LSMessage *message, char *command) {
  LSError lserror;
  LSErrorInit(&lserror);

  // Initialise the output buffer
  strcpy(run_command_buffer, "{\"stdOut\": [");

  // Run the command
  if (run_command(command, true)) {

    // Finalise the message ...
    strcat(run_command_buffer, "], \"returnValue\": true}");

    // fprintf(stderr, "Message is %s\n", run_command_buffer);

    // and send it to webOS.
    if (!LSMessageReply(lshandle, message, run_command_buffer, &lserror)) goto error;
  }
  else {

    // Finalise the command output ...
    strcat(run_command_buffer, "]");

    // and use it in a failure report message.
    if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto end;
  }

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Read a single string from a file, and return it to webOS.
//
static bool read_single_line(LSHandle* lshandle, LSMessage *message, char *file) {
  LSError lserror;
  LSErrorInit(&lserror);

  FILE *fp = fopen(file, "r");

  if (!fp) {
    sprintf(buffer, "{\"errorText\": \"Unable to open %s\", \"returnValue\": false, \"errorCode\": -1 }", file);
  }
  else {
    int value;
    if (fgets(line, MAXLINLEN-1, fp)) {
      sprintf(buffer, "{\"value\": \"%s\", \"returnValue\": true }", json_escape_str(line));
    }
    else {
      sprintf(buffer, "{\"errorText\": \"Unable to parse %s\", \"returnValue\": false, \"errorCode\": -1 }", file);
    }
    if (fclose(fp)) {
      sprintf(buffer, "{\"errorText\": \"Unable to close %s\", \"returnValue\": false, \"errorCode\": -1 }", file);
    }
  }

  fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Read a single integer from a file, and return it to webOS.
//
static bool read_single_integer(LSHandle* lshandle, LSMessage *message, char *file) {
  LSError lserror;
  LSErrorInit(&lserror);

  FILE *fp = fopen(file, "r");

  if (!fp) {
    sprintf(buffer, "{\"errorText\": \"Unable to open %s\", \"returnValue\": false, \"errorCode\": -1 }", file);
  }
  else {
    int value;
    if (fscanf(fp, "%d", &value) == 1) {
      sprintf(buffer, "{\"value\": %d, \"returnValue\": true }", value);
    }
    else {
      sprintf(buffer, "{\"errorText\": \"Unable to parse %s\", \"returnValue\": false, \"errorCode\": -1 }", file);
    }
    if (fclose(fp)) {
      sprintf(buffer, "{\"errorText\": \"Unable to close %s\", \"returnValue\": false, \"errorCode\": -1 }", file);
    }
  }

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Read /proc/cpuinfo
//
bool get_proc_cpuinfo_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message, "/bin/cat /proc/cpuinfo 2>&1");
}

//
// Read /proc/meminfo
//
bool get_proc_meminfo_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message, "/bin/cat /proc/meminfo 2>&1");
}

//
// Read /proc/loadavg
//
bool get_proc_loadavg_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message, "/bin/cat /proc/loadavg 2>&1");
}

//
// Read omap34xx_temp (Pre)
//
bool get_omap34xx_temp_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return read_single_integer(lshandle, message, "/sys/devices/platform/omap34xx_temp/temp1_input");
}

//
// Read tmp105_temp (Pixi)
//
bool get_tmp105_temp_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return read_single_integer(lshandle, message, "/sys/devices/platform/tmp105/celsius");
}

//
// Read /proc/sys/net/ipv4/tcp_congestion_control
//
bool get_tcp_congestion_control_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message, "/bin/cat /proc/sys/net/ipv4/tcp_congestion_control 2>&1");
}

//
// Write /proc/sys/net/ipv4/tcp_congestion_control
//
bool set_tcp_congestion_control_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  bool error = false;

  sprintf(buffer, "{\"returnValue\": true }");

  json_t *object = LSMessageGetPayloadJSON(message);

  // Extract the genericParams argument from the message
  json_t *value = json_find_first_label(object, "value");
  if (!value || (value->child->type != JSON_STRING)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing value\"}",
			&lserror)) goto error;
    return true;
  }

  sprintf(filename, "/proc/sys/net/ipv4/tcp_congestion_control");

  fprintf(stderr, "Writing %s to %s\n", value->child->text, filename);

  FILE *fp = fopen(filename, "w");
  if (!fp) {
    sprintf(errorText, "Unable to open %s", filename);
    error = true;
  }
  else {
    if (fputs(value->child->text, fp) < 0) {
      sprintf(errorText, "Unable to write to %s", filename);
      error = true;
    }
    if (fclose(fp)) {
      sprintf(errorText, "Unable to close %s", filename);
      error = true;
    }
  }
      
  if (error) {
    sprintf(buffer, "{\"errorText\": \"%s\", \"returnValue\": false, \"errorCode\": -1 }",
	    errorText);
  }
  
  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Read /proc/sys/net/ipv4/tcp_allowed_congestion_control
//
bool get_tcp_allowed_congestion_control_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message, "/bin/cat /proc/sys/net/ipv4/tcp_allowed_congestion_control 2>&1");
}

//
// Read /proc/sys/net/ipv4/tcp_available_congestion_control
//
bool get_tcp_available_congestion_control_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message, "/bin/cat /proc/sys/net/ipv4/tcp_available_congestion_control 2>&1");
}

//
// Read scaling_cur_freq
//
bool get_scaling_cur_freq_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return read_single_integer(lshandle, message, "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
}

//
// Read scaling_governor
//
bool get_scaling_governor_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return read_single_line(lshandle, message, "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
}

//
// Read cpufreq params
//
bool get_cpufreq_params_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);
  
  struct stat statbuf;

  bool error = false;
  char *governor = NULL;

  sprintf(buffer, "{\"returnValue\": true }");
  
  json_t *object = LSMessageGetPayloadJSON(message);

  // Extract the governor argument from the message
  json_t *param = json_find_first_label(object, "governor");
  if (param && (param->child->type == JSON_STRING)) {
    governor = param->child->text;
  }

  if (governor) {
    sprintf(directory, "%s/%s", cpufreqdir, governor);
  }
  else {
    sprintf(directory, "%s", cpufreqdir);
  }

  DIR *dp = opendir (directory);
  if (!dp) {
    // Don't give an actual errorCode, since it's expected for some governors.
    sprintf(buffer, "{\"errorText\": \"Unable to open %s\", \"returnValue\": false }", directory);
    // fprintf(stderr, "Message is %s\n", buffer);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }

  struct dirent *ep;
  bool first = true;
  sprintf(buffer, "{\"params\": [");
  
  while (ep = readdir (dp)) {
    if (!strcmp(ep->d_name, ".") || !strcmp(ep->d_name, "..") ||
	!strcmp(ep->d_name, "stats") || !strcmp(ep->d_name, "affected_cpus") ||
	!strcmp(ep->d_name, "scaling_driver")) {
      continue;
    }

    sprintf(filename, "%s/%s", directory, ep->d_name);

    bool writeable = false;
    bool directory = false;

    if (!stat(filename, &statbuf)) {
      if (statbuf.st_mode & S_IWUSR) {
	writeable = true;
      }
      if (statbuf.st_mode & S_IFDIR) {
	directory = true;
      }
    }
      
    strcpy(line,"");

    if (directory) {
      // Skip over other directories
      continue;
    }
    else {
      FILE *fp = fopen(filename, "r");
      if (!fp) {
	sprintf(errorText, "Unable to open %s", filename);
	error = true;
      }
      else {
	if (!fgets(line, MAXLINLEN-1, fp)) {
	  sprintf(errorText, "Unable to parse %s", filename);
	  error = true;
	}
	else {
	  line[strlen(line)-1] = '\0';
	}
	if (fclose(fp)) {
	  sprintf(errorText, "Unable to close %s", filename);
	  error = true;
	}
      }
	
      if (error) {
	sprintf(buffer, "{\"errorText\": \"%s\", \"returnValue\": false, \"errorCode\": -1 }",
		errorText);
	break;
      }
      else {
	sprintf(buffer+strlen(buffer), "%s{\"name\": \"%s\", \"writeable\": %s, \"value\": \"%s\"}",
		(first ? "" : ", "), ep->d_name, (writeable ? "true" : "false"), json_escape_str(line));
	first = false;
      }
    }
  }
  if (closedir(dp)) {
    sprintf(buffer, "{\"errorText\": \"Unable to close %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    directory);
    error = true;
  }

  if (!error) {
    strcat(buffer, "], \"returnValue\": ");
    strcat(buffer, error ? "false" : "true");
    if (governor) {
      strcat(buffer, ", \"governor\": \"");
      strcat(buffer, governor);
      strcat(buffer, "\"");
    }
    strcat(buffer, "}");
  }

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Write cpufreq params
//
bool set_cpufreq_params_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  bool error = false;
  char *governor = NULL;

  sprintf(buffer, "{\"returnValue\": true }");

  json_t *object = LSMessageGetPayloadJSON(message);

  // Extract the genericParams argument from the message
  json_t *genericParams = json_find_first_label(object, "genericParams");
  if (!genericParams || (genericParams->child->type != JSON_ARRAY)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing genericParams array\"}",
			&lserror)) goto error;
    return true;
  }

  // Extract the governorParams argument from the message
  json_t *governorParams = json_find_first_label(object, "governorParams");
  if (!governorParams || (governorParams->child->type != JSON_ARRAY)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing governorParams array\"}",
			&lserror)) goto error;
    return true;
  }

  sprintf(directory, "%s", cpufreqdir);
  json_t *genericEntry = genericParams->child->child;
  while (genericEntry) {
    if (genericEntry->type != JSON_OBJECT) {
      if (!LSMessageReply(lshandle, message,
			  "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing genericParams array element\"}",
			  &lserror)) goto error;
      return true;
    }
    json_t *name = json_find_first_label(genericEntry, "name");
    if (!name || (name->child->type != JSON_STRING) ||
	(strspn(name->child->text, ALLOWED_CHARS) != strlen(name->child->text))) {
      if (!LSMessageReply(lshandle, message,
			  "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing name genericEntry\"}",
			  &lserror)) goto error;
      return true;
    }
    json_t *value = json_find_first_label(genericEntry, "value");
    if (!value || (value->child->type != JSON_STRING) ||
	(strspn(value->child->text, ALLOWED_CHARS) != strlen(value->child->text))) {
      if (!LSMessageReply(lshandle, message,
			  "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing value genericEntry\"}",
			  &lserror)) goto error;
      return true;
    }

    if (!strcmp(name->child->text, "scaling_governor")) {
      governor = value->child->text;
    }

    sprintf(filename, "%s/%s", directory, name->child->text);

    fprintf(stderr, "Writing %s to %s\n", value->child->text, filename);

    FILE *fp = fopen(filename, "w");
    if (!fp) {
      sprintf(errorText, "Unable to open %s", filename);
      error = true;
    }
    else {
      if (fputs(value->child->text, fp) < 0) {
	sprintf(errorText, "Unable to write to %s", filename);
	error = true;
      }
      if (fclose(fp)) {
	sprintf(errorText, "Unable to close %s", filename);
	error = true;
      }
    }
      
    if (error) {
      sprintf(buffer, "{\"errorText\": \"%s\", \"returnValue\": false, \"errorCode\": -1 }",
	      errorText);
      break;
    }
    
    genericEntry = genericEntry->next;
  }

  if (governor != NULL) {

    sprintf(directory, "%s/%s", cpufreqdir, governor);
    json_t *governorEntry = governorParams->child->child;
    while (governorEntry) {
      if (governorEntry->type != JSON_OBJECT) {
	if (!LSMessageReply(lshandle, message,
			    "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing governorParams array element\"}",
			    &lserror)) goto error;
	return true;
      }
      json_t *name = json_find_first_label(governorEntry, "name");
      if (!name || (name->child->type != JSON_STRING) ||
	  (strspn(name->child->text, ALLOWED_CHARS) != strlen(name->child->text))) {
	if (!LSMessageReply(lshandle, message,
			    "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing name governorEntry\"}",
			    &lserror)) goto error;
	return true;
      }
      json_t *value = json_find_first_label(governorEntry, "value");
      if (!value || (value->child->type != JSON_STRING) ||
	  (strspn(value->child->text, ALLOWED_CHARS) != strlen(value->child->text))) {
	if (!LSMessageReply(lshandle, message,
			    "{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing value governorEntry\"}",
			    &lserror)) goto error;
	return true;
      }

      sprintf(filename, "%s/%s", directory, name->child->text);

      fprintf(stderr, "Writing %s to %s\n", value->child->text, filename);

      FILE *fp = fopen(filename, "w");
      if (!fp) {
	sprintf(errorText, "Unable to open %s", filename);
	error = true;
      }
      else {
	if (fputs(value->child->text, fp) < 0) {
	  sprintf(errorText, "Unable to write to %s", filename);
	  error = true;
	}
	if (fclose(fp)) {
	  sprintf(errorText, "Unable to close %s", filename);
	  error = true;
	}
      }
      
      if (error) {
	sprintf(buffer, "{\"errorText\": \"%s\", \"returnValue\": false, \"errorCode\": -1 }",
		errorText);
	break;
      }
    
      governorEntry = governorEntry->next;
    }
  }

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Write upstart script to make cpufreq params "sticky"
//
bool stick_cpufreq_params_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  bool error = false;
  char *governor = NULL;

  sprintf(buffer, "{\"returnValue\": true }");

  json_t *object = LSMessageGetPayloadJSON(message);

  // Extract the genericParams argument from the message
  json_t *genericParams = json_find_first_label(object, "genericParams");
  if (!genericParams || (genericParams->child->type != JSON_ARRAY)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing genericParams array\"}",
			&lserror)) goto error;
    return true;
  }

  // Extract the governorParams argument from the message
  json_t *governorParams = json_find_first_label(object, "governorParams");
  if (!governorParams || (governorParams->child->type != JSON_ARRAY)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing governorParams array\"}",
			&lserror)) goto error;
    return true;
  }

  sprintf(filename, "/var/palm/event.d/org.webosinternals.govnah-settings");
  FILE *fp = fopen(filename, "w");
  if (!fp) {
    sprintf(buffer,
	    "{\"errorText\": \"Unable to open %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    filename);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }

  error = false;
  if (fputs("description \"Govnah Settings\"\n", fp) < 0) error = true;
  if (fputs("\n", fp) < 0) error = true;
  if (fputs("start on stopped finish\n", fp) < 0) error = true;
  if (fputs("\n", fp) < 0) error = true;
  if (fputs("script\n", fp) < 0) error = true;
  if (fputs("\n", fp) < 0) error = true;
  if (fputs("[ \"`/usr/bin/lunaprop -m com.palm.properties.prevBootPanicked`\" = \"false\" ] || exit 0\n", fp) < 0) error = true;
  if (fputs("[ \"`/usr/bin/lunaprop -m com.palm.properties.prevShutdownClean`\" = \"true\" ] || exit 0\n", fp) < 0) error = true;
  if (fputs("[ \"`/usr/bin/lunaprop -m -n com.palm.system last_umount_clean`\"  = \"true\" ] || exit 0\n", fp) < 0) error = true;
  if (fputs("\n", fp) < 0) error = true;
 
  if (error) {
    (void)fclose(fp);
    (void)unlink(filename);
    sprintf(buffer,
	    "{\"errorText\": \"Unable to write to %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    filename);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }
  
  sprintf(directory, "%s", cpufreqdir);
  json_t *genericEntry = genericParams->child->child;
  while (genericEntry) {
    if (genericEntry->type != JSON_OBJECT) continue;
    json_t *name = json_find_first_label(genericEntry, "name");
    if (!name || (name->child->type != JSON_STRING) ||
	(strspn(name->child->text, ALLOWED_CHARS) != strlen(name->child->text))) continue;
    json_t *value = json_find_first_label(genericEntry, "value");
    if (!value || (value->child->type != JSON_STRING) ||
	(strspn(value->child->text, ALLOWED_CHARS) != strlen(value->child->text)))  continue;

    if (!strcmp(name->child->text, "scaling_governor")) {
      governor = value->child->text;
    }

    fprintf(stderr, "echo %s > %s/%s\n", value->child->text, directory, name->child->text);
    sprintf(line, "echo -n '%s' > %s/%s\n", value->child->text, directory, name->child->text);

    if (fputs(line, fp) < 0) {
      (void)fclose(fp);
      (void)unlink(filename);
      sprintf(buffer,
	      "{\"errorText\": \"Unable to open %s\", \"returnValue\": false, \"errorCode\": -1 }",
	      filename);
      if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
      return true;
    }
      
    genericEntry = genericEntry->next;
  }

  if (governor != NULL) {

    sprintf(directory, "%s/%s", cpufreqdir, governor);
    json_t *governorEntry = governorParams->child->child;
    while (governorEntry) {
      if (governorEntry->type != JSON_OBJECT) continue;
      json_t *name = json_find_first_label(governorEntry, "name");
      if (!name || (name->child->type != JSON_STRING) ||
	  (strspn(name->child->text, ALLOWED_CHARS) != strlen(name->child->text))) continue;
      json_t *value = json_find_first_label(governorEntry, "value");
      if (!value || (value->child->type != JSON_STRING) ||
	  (strspn(value->child->text, ALLOWED_CHARS) != strlen(value->child->text))) continue;

      fprintf(stderr, "echo %s > %s/%s\n", value->child->text, directory, name->child->text);
      sprintf(line, "echo -n '%s' > %s/%s\n", value->child->text, directory, name->child->text);

      if (fputs(line, fp) < 0) {
	(void)fclose(fp);
	(void)unlink(filename);
	sprintf(buffer,
		"{\"errorText\": \"Unable to write to %s\", \"returnValue\": false, \"errorCode\": -1 }",
		filename);
	if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
	return true;
      }
      
      governorEntry = governorEntry->next;
    }
  }

  if (fputs("\n", fp) < 0) error = true;
  if (fputs("end script\n", fp) < 0) error = true;
  if (error) {
    (void)fclose(fp);
    (void)unlink(filename);
    sprintf(buffer,
	    "{\"errorText\": \"Unable to write to %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    filename);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }
  
  if (fclose(fp)) {
    sprintf(buffer,
	    "{\"errorText\": \"Unable to close %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    filename);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Delete cpufreq params upstart script
//
bool unstick_cpufreq_params_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  sprintf(buffer, "{\"returnValue\": true }");

  sprintf(filename, "/var/palm/event.d/org.webosinternals.govnah-settings");
  (void)unlink(filename);

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Read time_in_state
//
bool get_time_in_state_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message,
			"/bin/cat /sys/devices/system/cpu/cpu0/cpufreq/stats/time_in_state 2>&1");
}

//
// Read total_trans
//
bool get_total_trans_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message,
			"/bin/cat /sys/devices/system/cpu/cpu0/cpufreq/stats/total_trans 2>&1");
}

//
// Read trans_table
//
bool get_trans_table_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  return simple_command(lshandle, message,
			"/bin/cat /sys/devices/system/cpu/cpu0/cpufreq/stats/trans_table 2>&1");
}

//
// Read compcache configuration
//
bool get_compcache_config_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);
  
  char command[MAXLINLEN];

  sprintf(buffer, "{\"returnValue\": true }");
  
  strcpy(run_command_buffer, "/lib/modules/");
  if (!run_command("/bin/uname -r", false)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Unable to determine kernel version\"}",
			&lserror)) goto error;
    return true;
  }
  sprintf(command, "/usr/bin/test -f %s/extra/ramzswap.ko", run_command_buffer);
  strcpy(run_command_buffer, "");
  if (!run_command(command, false)) {
    sprintf(buffer, "{\"params\": [], \"returnValue\": true }");
  }
  else {
    strcpy(run_command_buffer, "");
    if (run_command("/bin/grep MemLimit /proc/ramzswap 2>/dev/null | awk '{print $2}'", false) && run_command_buffer[0]) {
      sprintf(buffer, "{\"params\": [{\"name\":\"compcache_enabled\", \"value\": \"1\", \"writeable\": true}, {\"name\": \"compcache_memlimit\", \"value\": \"%s\", \"writeable\": true}], \"returnValue\": true }", run_command_buffer);
    }
    else {
      sprintf(buffer, "{\"params\": [{\"name\":\"compcache_enabled\", \"value\": \"0\", \"writeable\": true}, {\"name\": \"compcache_memlimit\", \"value\": \"16384\", \"writeable\": true}], \"returnValue\": true }");
    }
  }

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Write compcache configuration
//
bool set_compcache_config_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  char command[MAXLINLEN];

  sprintf(buffer, "{\"returnValue\": true }");

  json_t *object = LSMessageGetPayloadJSON(message);

  bool enable = false;
  char *memlimit = NULL;

  // Extract the compcacheConfig argument from the message
  json_t *compcacheConfig = json_find_first_label(object, "compcacheConfig");
  if (!compcacheConfig || (compcacheConfig->child->type != JSON_ARRAY)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing compcacheConfig array\"}",
			&lserror)) goto error;
    return true;
  }

  json_t *entry = compcacheConfig->child->child;
  while (entry) {
    if (entry->type != JSON_OBJECT) continue;
    json_t *name = json_find_first_label(entry, "name");
    if (!name || (name->child->type != JSON_STRING) ||
	(strspn(name->child->text, ALLOWED_CHARS) != strlen(name->child->text))) continue;
    json_t *value = json_find_first_label(entry, "value");
    if (!value || (value->child->type != JSON_STRING) ||
	(strspn(value->child->text, ALLOWED_CHARS) != strlen(value->child->text)))  continue;

    if (!strcmp(name->child->text, "compcache_enabled")) {
      enable = strcmp(value->child->text, "1")?false:true;
    }

    if (!strcmp(name->child->text, "compcache_memlimit")) {
      memlimit = value->child->text;
    }

    entry = entry->next;
  }

  if (!memlimit) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing memlimit\"}",
			&lserror)) goto error;
    return true;
  }

  strcpy(run_command_buffer, "/lib/modules/");
  if (!run_command("/bin/uname -r", false)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Unable to determine kernel version\"}",
			&lserror)) goto error;
    return true;
  }
  strcpy(directory, run_command_buffer);

  bool enabled = false;
  strcpy(run_command_buffer, "");
  if (run_command("/bin/grep MemLimit /proc/ramzswap 2>/dev/null | awk '{print $2}'", false) && run_command_buffer[0]) {
    enabled = true;
  }

  if (!enabled && enable) {
    strcpy(command, "/sbin/swapoff -a 2>&1");
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command(command, true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
    sprintf(command, "/sbin/insmod %s/extra/xvmalloc.ko 2>&1", directory);
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command(command, true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
    sprintf(command, "/sbin/insmod %s/extra/ramzswap.ko backing_swap=/dev/mapper/store-swap memlimit_kb=%s 2>&1", directory, memlimit);
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command(command, true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
    sprintf(command, "/bin/sleep 3 2>&1");
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command(command, true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
    strcpy(command, "/sbin/swapon /dev/ramzswap0 -p 0 2>&1");
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command(command, true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
  }
  else if (enabled && !enable) {
    strcpy(command, "/sbin/swapoff -a 2>&1");
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command(command, true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
    strcpy(command, "/sbin/rmmod ramzswap 2>&1");
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command(command, true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
    strcpy(command, "/sbin/rmmod xvmalloc 2>&1");
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command("/sbin/rmmod xvmalloc", true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
    strcpy(command, "/sbin/swapon /dev/mapper/store-swap -p 0 2>&1");
    strcpy(run_command_buffer, "{\"stdOut\": [");
    if (!run_command(command, true)) {
      strcat(run_command_buffer, "]");
      if (!report_command_failure(lshandle, message, command, run_command_buffer+11, NULL)) goto error;
      return true;
    }
  }

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Write upstart script to make compcache configuration "sticky"
//
bool stick_compcache_config_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  bool error = false;
  char *governor = NULL;

  sprintf(buffer, "{\"returnValue\": true }");

  json_t *object = LSMessageGetPayloadJSON(message);

  bool enable = false;
  char *memlimit = NULL;

  // Extract the compcacheConfig argument from the message
  json_t *compcacheConfig = json_find_first_label(object, "compcacheConfig");
  if (!compcacheConfig || (compcacheConfig->child->type != JSON_ARRAY)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing compcacheConfig array\"}",
			&lserror)) goto error;
    return true;
  }

  json_t *entry = compcacheConfig->child->child;
  while (entry) {
    if (entry->type != JSON_OBJECT) continue;
    json_t *name = json_find_first_label(entry, "name");
    if (!name || (name->child->type != JSON_STRING) ||
	(strspn(name->child->text, ALLOWED_CHARS) != strlen(name->child->text))) continue;
    json_t *value = json_find_first_label(entry, "value");
    if (!value || (value->child->type != JSON_STRING) ||
	(strspn(value->child->text, ALLOWED_CHARS) != strlen(value->child->text)))  continue;

    if (!strcmp(name->child->text, "compcache_enabled")) {
      enable = strcmp(value->child->text, "1")?false:true;
    }

    if (!strcmp(name->child->text, "compcache_memlimit")) {
      memlimit = value->child->text;
    }

    entry = entry->next;
  }

  if (!memlimit) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing memlimit\"}",
			&lserror)) goto error;
    return true;
  }

  sprintf(filename, "/var/palm/event.d/org.webosinternals.govnah-compcache");

  if (!enable) {
    (void)unlink(filename);
    // fprintf(stderr, "Message is %s\n", buffer);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }

  FILE *fp = fopen(filename, "w");
  if (!fp) {
    sprintf(buffer,
	    "{\"errorText\": \"Unable to open %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    filename);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }

  error = false;
  if (fputs("description \"Govnah CompCache Configuration\"\n", fp) < 0) error = true;
  if (fputs("\n", fp) < 0) error = true;
  if (fputs("start on stopped finish\n", fp) < 0) error = true;
  if (fputs("stop on runlevel [!2]\n", fp) < 0) error = true;
  if (fputs("\n", fp) < 0) error = true;
  if (fputs("script\n", fp) < 0) error = true;
  if (fputs("\n", fp) < 0) error = true;
  if (fputs("[ \"`/usr/bin/lunaprop -m com.palm.properties.prevBootPanicked`\" = \"false\" ] || exit 0\n", fp) < 0) error = true;
  if (fputs("[ \"`/usr/bin/lunaprop -m com.palm.properties.prevShutdownClean`\" = \"true\" ] || exit 0\n", fp) < 0) error = true;
  if (fputs("[ \"`/usr/bin/lunaprop -m -n com.palm.system last_umount_clean`\"  = \"true\" ] || exit 0\n", fp) < 0) error = true;
  if (fputs("\n", fp) < 0) error = true;
 
  if (error) {
    (void)fclose(fp);
    (void)unlink(filename);
    sprintf(buffer,
	    "{\"errorText\": \"Unable to write to %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    filename);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }
  
  if (fputs("swapoff -a\n", fp) < 0) error = true;
  if (fputs("insmod /lib/modules/`uname -r`/extra/xvmalloc.ko\n", fp) < 0) error = true;
  sprintf(line,
	  "insmod /lib/modules/`uname -r`/extra/ramzswap.ko memlimit_kb=%s backing_swap=/dev/mapper/store-swap\n",
	  memlimit);
  if (fputs(line, fp) < 0) error = true;
  if (fputs("sleep 3\n", fp) < 0) error = true;
  if (fputs("swapon /dev/ramzswap0 -p 1\n", fp) < 0) error = true;

  if (fputs("\n", fp) < 0) error = true;
  if (fputs("end script\n", fp) < 0) error = true;
  if (error) {
    (void)fclose(fp);
    (void)unlink(filename);
    sprintf(buffer,
	    "{\"errorText\": \"Unable to write to %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    filename);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }
  
  if (fclose(fp)) {
    sprintf(buffer,
	    "{\"errorText\": \"Unable to close %s\", \"returnValue\": false, \"errorCode\": -1 }",
	    filename);
    if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;
    return true;
  }

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Delete compcache config upstart script
//
bool unstick_compcache_config_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  sprintf(buffer, "{\"returnValue\": true }");

  sprintf(filename, "/var/palm/event.d/org.webosinternals.govnah-compcache");
  (void)unlink(filename);

  // fprintf(stderr, "Message is %s\n", buffer);
  if (!LSMessageReply(lshandle, message, buffer, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Handler for the getProfiles service.
//
bool getProfiles_handler(LSHandle* lshandle, LSMessage *reply, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessage* message = (LSMessage*)ctx;
  retVal = LSMessageRespond(message, LSMessageGetPayload(reply), &lserror);
  LSMessageUnref(message);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Call the getProfiles and return the output to webOS.
//
bool getProfiles_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  json_t *object = LSMessageGetPayloadJSON(message);

  // Extract the params argument from the message
  json_t *id = json_find_first_label(object, "returnid");
  if (!id || (id->child->type != JSON_STRING)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing returnid\"}",
			&lserror)) goto error;
    return true;
  }

  sprintf(line, "{\"id\":\"org.webosinternals.govnah\",\"params\":{\"type\":\"get-profiles\",\"returnid\":\"%s\"}}",
	  id->child->text);

  LSMessageRef(message);
  if (!LSCall(priv_serviceHandle, "palm://com.palm.applicationManager/launch", line,
	      getProfiles_handler, message, NULL, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

//
// Handler for the setProfile service.
//
bool setProfile_handler(LSHandle* lshandle, LSMessage *reply, void *ctx) {
  bool retVal;
  LSError lserror;
  LSErrorInit(&lserror);
  LSMessage* message = (LSMessage*)ctx;
  retVal = LSMessageRespond(message, LSMessageGetPayload(reply), &lserror);
  LSMessageUnref(message);
  if (!retVal) {
    LSErrorPrint(&lserror, stderr);
    LSErrorFree(&lserror);
  }
  return retVal;
}

//
// Call the setProfile and return the output to webOS.
//
bool setProfile_method(LSHandle* lshandle, LSMessage *message, void *ctx) {
  LSError lserror;
  LSErrorInit(&lserror);

  json_t *object = LSMessageGetPayloadJSON(message);

  // Extract the params argument from the message
  json_t *id = json_find_first_label(object, "profileid");
  if (!id || (id->child->type != JSON_NUMBER)) {
    if (!LSMessageReply(lshandle, message,
			"{\"returnValue\": false, \"errorCode\": -1, \"errorText\": \"Invalid or missing profileid\"}",
			&lserror)) goto error;
    return true;
  }

  sprintf(line, "{\"id\":\"org.webosinternals.govnah\",\"params\":{\"type\":\"set-profile\",\"profileid\":%s}}",
	  id->child->text);

  LSMessageRef(message);
  if (!LSCall(priv_serviceHandle, "palm://com.palm.applicationManager/launch", line,
	      getProfiles_handler, message, NULL, &lserror)) goto error;

  return true;
 error:
  LSErrorPrint(&lserror, stderr);
  LSErrorFree(&lserror);
 end:
  return false;
}

LSMethod luna_methods[] = {
  { "status",			dummy_method },

  { "get_proc_cpuinfo",		get_proc_cpuinfo_method },
  { "get_proc_meminfo",		get_proc_meminfo_method },
  { "get_proc_loadavg",		get_proc_loadavg_method },
  { "get_omap34xx_temp",	get_omap34xx_temp_method },
  { "get_tmp105_temp",		get_tmp105_temp_method },

  { "get_tcp_congestion_control", get_tcp_congestion_control_method },
  { "set_tcp_congestion_control", set_tcp_congestion_control_method },
  { "get_tcp_allowed_congestion_control", get_tcp_allowed_congestion_control_method },
  { "get_tcp_available_congestion_control", get_tcp_available_congestion_control_method },

  { "get_scaling_cur_freq",     get_scaling_cur_freq_method },
  { "get_scaling_governor",     get_scaling_governor_method },
  { "get_cpufreq_params",	get_cpufreq_params_method },
  { "set_cpufreq_params",	set_cpufreq_params_method },
  { "stick_cpufreq_params",	stick_cpufreq_params_method },
  { "unstick_cpufreq_params",	unstick_cpufreq_params_method },
  { "get_time_in_state",	get_time_in_state_method },
  { "get_total_trans",		get_total_trans_method },
  { "get_trans_table",		get_trans_table_method },

  { "get_compcache_config",	get_compcache_config_method },
  { "set_compcache_config",	set_compcache_config_method },
  { "stick_compcache_config",	stick_compcache_config_method },
  { "unstick_compcache_config",	unstick_compcache_config_method },

  { "getProfiles",		getProfiles_method },
  { "setProfile",		setProfile_method },
  { 0, 0 }
};

bool register_methods(LSPalmService *serviceHandle, LSError lserror) {
  return LSPalmServiceRegisterCategory(serviceHandle, "/", luna_methods,
				       NULL, NULL, NULL, &lserror);
}
