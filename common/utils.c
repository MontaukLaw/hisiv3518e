#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <netinet/if_ether.h>
#include <net/if.h>

#include <linux/if_ether.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "sample_comm.h"

int parse_request_string(char const *reqStr, unsigned reqStrSize,
                         char *resultCmdName, unsigned resultCmdNameMaxSize,
                         char *resultURLPreSuffix, unsigned resultURLPreSuffixMaxSize,
                         char *resultURLSuffix, unsigned resultURLSuffixMaxSize,
                         char *resultCSeq, unsigned resultCSeqMaxSize)
{
    // This parser is currently rather dumb; it should be made smarter #####

    // Read everything up to the first space as the command name:
    int parseSucceeded = FALSE;
    unsigned i;
    for (i = 0; i < resultCmdNameMaxSize - 1 && i < reqStrSize; ++i)
    {
        char c = reqStr[i];
        if (c == ' ' || c == '\t')
        {
            parseSucceeded = TRUE;
            break;
        }

        resultCmdName[i] = c;
    }
    resultCmdName[i] = '\0';
    if (!parseSucceeded)
        return FALSE;

    // Skip over the prefix of any "rtsp://" or "rtsp:/" URL that follows:
    unsigned j = i + 1;
    while (j < reqStrSize && (reqStr[j] == ' ' || reqStr[j] == '\t'))
        ++j; // skip over any additional white space
    for (j = i + 1; j < reqStrSize - 8; ++j)
    {
        if ((reqStr[j] == 'r' || reqStr[j] == 'R') && (reqStr[j + 1] == 't' || reqStr[j + 1] == 'T') && (reqStr[j + 2] == 's' || reqStr[j + 2] == 'S') && (reqStr[j + 3] == 'p' || reqStr[j + 3] == 'P') && reqStr[j + 4] == ':' && reqStr[j + 5] == '/')
        {
            j += 6;
            if (reqStr[j] == '/')
            {
                // This is a "rtsp://" URL; skip over the host:port part that follows:
                ++j;
                while (j < reqStrSize && reqStr[j] != '/' && reqStr[j] != ' ')
                    ++j;
            }
            else
            {
                // This is a "rtsp:/" URL; back up to the "/":
                --j;
            }
            i = j;
            break;
        }
    }

    // Look for the URL suffix (before the following "RTSP/"):
    parseSucceeded = FALSE;
    unsigned k;
    for (k = i + 1; k < reqStrSize - 5; ++k)
    {
        if (reqStr[k] == 'R' && reqStr[k + 1] == 'T' &&
            reqStr[k + 2] == 'S' && reqStr[k + 3] == 'P' && reqStr[k + 4] == '/')
        {
            while (--k >= i && reqStr[k] == ' ')
            {
            } // go back over all spaces before "RTSP/"
            unsigned k1 = k;
            while (k1 > i && reqStr[k1] != '/' && reqStr[k1] != ' ')
                --k1;
            // the URL suffix comes from [k1+1,k]

            // Copy "resultURLSuffix":
            if (k - k1 + 1 > resultURLSuffixMaxSize)
                return FALSE; // there's no room
            unsigned n = 0, k2 = k1 + 1;
            while (k2 <= k)
                resultURLSuffix[n++] = reqStr[k2++];
            resultURLSuffix[n] = '\0';

            // Also look for the URL 'pre-suffix' before this:
            unsigned k3 = --k1;
            while (k3 > i && reqStr[k3] != '/' && reqStr[k3] != ' ')
                --k3;
            // the URL pre-suffix comes from [k3+1,k1]

            // Copy "resultURLPreSuffix":
            if (k1 - k3 + 1 > resultURLPreSuffixMaxSize)
                return FALSE; // there's no room
            n = 0;
            k2 = k3 + 1;
            while (k2 <= k1)
                resultURLPreSuffix[n++] = reqStr[k2++];
            resultURLPreSuffix[n] = '\0';

            i = k + 7; // to go past " RTSP/"
            parseSucceeded = TRUE;
            break;
        }
    }
    if (!parseSucceeded)
        return FALSE;

    // Look for "CSeq:", skip whitespace,
    // then read everything up to the next \r or \n as 'CSeq':
    parseSucceeded = FALSE;
    for (j = i; j < reqStrSize - 5; ++j)
    {
        if (reqStr[j] == 'C' && reqStr[j + 1] == 'S' && reqStr[j + 2] == 'e' &&
            reqStr[j + 3] == 'q' && reqStr[j + 4] == ':')
        {
            j += 5;
            unsigned n;
            while (j < reqStrSize && (reqStr[j] == ' ' || reqStr[j] == '\t'))
                ++j;
            for (n = 0; n < resultCSeqMaxSize - 1 && j < reqStrSize; ++n, ++j)
            {
                char c = reqStr[j];
                if (c == '\r' || c == '\n')
                {
                    parseSucceeded = TRUE;
                    break;
                }

                resultCSeq[n] = c;
            }
            resultCSeq[n] = '\0';
            break;
        }
    }
    if (!parseSucceeded)
        return FALSE;

    return TRUE;
}