
/***************************************************************************
 * FingerPrintResults.cc -- The FingerPrintResults class the results of OS *
 * fingerprint matching against a certain host.                            *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1996-2020 Insecure.Com LLC ("The Nmap  *
 * Project"). Nmap is also a registered trademark of the Nmap Project.     *
 *                                                                         *
 * This program is distributed under the terms of the Nmap Public Source   *
 * License (NPSL). The exact license text applying to a particular Nmap    *
 * release or source code control revision is contained in the LICENSE     *
 * file distributed with that version of Nmap or source code control       *
 * revision. More Nmap copyright/legal information is available from       *
 * https://nmap.org/book/man-legal.html, and further information on the    *
 * NPSL license itself can be found at https://nmap.org/npsl. This header  *
 * summarizes some key points from the Nmap license, but is no substitute  *
 * for the actual license text.                                            *
 *                                                                         *
 * Nmap is generally free for end users to download and use themselves,    *
 * including commercial use. It is available from https://nmap.org.        *
 *                                                                         *
 * The Nmap license generally prohibits companies from using and           *
 * redistributing Nmap in commercial products, but we sell a special Nmap  *
 * OEM Edition with a more permissive license and special features for     *
 * this purpose. See https://nmap.org/oem                                  *
 *                                                                         *
 * If you have received a written Nmap license agreement or contract       *
 * stating terms other than these (such as an Nmap OEM license), you may   *
 * choose to use and redistribute Nmap under those terms instead.          *
 *                                                                         *
 * The official Nmap Windows builds include the Npcap software             *
 * (https://npcap.org) for packet capture and transmission. It is under    *
 * separate license terms which forbid redistribution without special      *
 * permission. So the official Nmap Windows builds may not be              *
 * redistributed without special permission (such as an Nmap OEM           *
 * license).                                                               *
 *                                                                         *
 * Source is provided to this software because we believe users have a     *
 * right to know exactly what a program is going to do before they run it. *
 * This also allows you to audit the software for security holes.          *
 *                                                                         *
 * Source code also allows you to port Nmap to new platforms, fix bugs,    *
 * and add new features.  You are highly encouraged to submit your         *
 * changes as a Github PR or by email to the dev@nmap.org mailing list     *
 * for possible incorporation into the main distribution. Unless you       *
 * specify otherwise, it is understood that you are offering us very       *
 * broad rights to use your submissions as described in the Nmap Public    *
 * Source License Contributor Agreement. This is important because we      *
 * fund the project by selling licenses with various terms, and also       *
 * because the inability to relicense code has caused devastating          *
 * problems for other Free Software projects (such as KDE and NASM).       *
 *                                                                         *
 * The free version of Nmap is distributed in the hope that it will be     *
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. Warranties,        *
 * indemnification and commercial support are all available through the    *
 * Npcap OEM program--see https://nmap.org/oem.                            *
 *                                                                         *
 ***************************************************************************/

/* $Id: FingerPrintResults.cc 38085 2020-10-05 23:00:30Z dmiller $ */

#include "FingerPrintResults.h"
#include "osscan.h"
#include "NmapOps.h"

extern NmapOps o;

FingerPrintResults::FingerPrintResults() {
  num_perfect_matches = num_matches = 0;
  overall_results = OSSCAN_NOMATCHES;
  memset(accuracy, 0, sizeof(accuracy));
  isClassified = false;
  osscan_opentcpport = osscan_closedtcpport = osscan_closedudpport = -1;
  distance = -1;
  distance_guess = -1;
  distance_calculation_method = DIST_METHOD_NONE;
  maxTimingRatio = 0;
  incomplete = false;
}

FingerPrintResults::~FingerPrintResults() {
}

FingerPrintResultsIPv4::FingerPrintResultsIPv4() {
  FPs = (FingerPrint **) safe_zalloc(o.maxOSTries() * sizeof(FingerPrint *));
  numFPs = 0;
}

FingerPrintResultsIPv4::~FingerPrintResultsIPv4() {
  int i;

  /* Free OS fingerprints of OS scanning was done */
  for(i=0; i < numFPs; i++) {
    delete(FPs[i]);
    FPs[i] = NULL;
  }
  numFPs = 0;
  free(FPs);
}

FingerPrintResultsIPv6::FingerPrintResultsIPv6() {
  unsigned int i;

  begin_time.tv_sec = 0;
  begin_time.tv_usec = 0;
  for (i = 0; i < sizeof(fp_responses) / sizeof(*fp_responses); i++)
    fp_responses[i] = NULL;
  flow_label = 0;
}

FingerPrintResultsIPv6::~FingerPrintResultsIPv6() {
  unsigned int i;

  for (i = 0; i < sizeof(fp_responses) / sizeof(*fp_responses); i++) {
    if (fp_responses[i])
      delete fp_responses[i];
  }
}

const struct OS_Classification_Results *FingerPrintResults::getOSClassification() {
  if (!isClassified) { populateClassification(); isClassified = true; }
  return &OSR;
}

/* If the fingerprint is of potentially poor quality, we don't want to
   print it and ask the user to submit it.  In that case, the reason
   for skipping the FP is returned as a static string.  If the FP is
   great and should be printed, NULL is returned. */
const char *FingerPrintResults::OmitSubmissionFP() {
  static char reason[128];

  if (o.scan_delay > 500) { // This can screw up the sequence timing
    Snprintf(reason, sizeof(reason), "Scan delay (%d) is greater than 500", o.scan_delay);
    return reason;
  }

  if (o.timing_level > 4)
    return "Timing level 5 (Insane) used";

  if (osscan_opentcpport <= 0)
    return "Missing an open TCP port so results incomplete";

  if (osscan_closedtcpport <= 0)
    return "Missing a closed TCP port so results incomplete";

  /* This can happen if the TTL in the response to the UDP probe is somehow
     greater than the TTL in the probe itself. We exclude -1 because that is
     used to mean the distance is unknown, though there's a chance it could
     have come from the distance calculation. */
  if (distance < -1) {
    Snprintf(reason, sizeof(reason), "Host distance (%d network hops) appears to be negative", distance);
    return reason;
  }

  if (distance > 5) {
    Snprintf(reason, sizeof(reason), "Host distance (%d network hops) is greater than five", distance);
    return reason;
  }

  if (maxTimingRatio > 1.4) {
    Snprintf(reason, sizeof(reason), "maxTimingRatio (%e) is greater than 1.4", maxTimingRatio);
    return reason;
  }

  if (osscan_closedudpport < 0 && !o.udpscan) {
    /* If we didn't get a U1 response, that might be just
       because we didn't search for an closed port rather than
       because this OS doesn't respond to that sort of probe.
       So we don't print FP if U1 response is lacking AND no UDP
       scan was performed. */
    return "Didn't receive UDP response. Please try again with -sSU";
  }

  if (incomplete) {
    return "Some probes failed to send so results incomplete";
  }

  return NULL;
}

/* IPv6 classification is more robust to errors than IPv4, so apply less
   stringent conditions than the general OmitSubmissionFP. */
const char *FingerPrintResultsIPv6::OmitSubmissionFP() {
  static char reason[128];

  if (o.scan_delay > 500) { // This can screw up the sequence timing
    Snprintf(reason, sizeof(reason), "Scan delay (%d) is greater than 500", o.scan_delay);
    return reason;
  }

  if (osscan_opentcpport <= 0 && osscan_closedtcpport <= 0) {
    return "Missing a closed or open TCP port so results incomplete";
  }

  if (incomplete) {
    return "Some probes failed to send so results incomplete";
  }

  return NULL;
}


/* Goes through fingerprinting results to populate OSR */
void FingerPrintResults::populateClassification() {
  std::vector<OS_Classification>::iterator osclass;
  int printno;

  OSR.OSC_num_perfect_matches = OSR.OSC_num_matches = 0;
  OSR.overall_results = OSSCAN_SUCCESS;

  if (overall_results == OSSCAN_TOOMANYMATCHES) {
    // The normal classification overflowed so we don't even have all the perfect matches,
    // I don't see any good reason to do classification.
    OSR.overall_results = OSSCAN_TOOMANYMATCHES;
    return;
  }

  for(printno = 0; printno < num_matches; printno++) {
    // a single print may have multiple classifications
    for (osclass = matches[printno]->OS_class.begin();
         osclass != matches[printno]->OS_class.end();
         osclass++) {
      if (!classAlreadyExistsInResults(&*osclass)) {
        // Then we have to add it ... first ensure we have room
        if (OSR.OSC_num_matches == MAX_FP_RESULTS) {
          // Out of space ... if the accuracy of this one is 100%, we have a problem
          if (printno < num_perfect_matches)
            OSR.overall_results = OSSCAN_TOOMANYMATCHES;
          return;
        }

        // We have space, but do we even want this one?  No point
        // including lesser matches if we have 1 or more perfect
        // matches.
        if (OSR.OSC_num_perfect_matches > 0 && printno >= num_perfect_matches) {
          return;
        }

        // OK, we will add the new class
        OSR.OSC[OSR.OSC_num_matches] = &*osclass;
        OSR.OSC_Accuracy[OSR.OSC_num_matches] = accuracy[printno];
        if (printno < num_perfect_matches)
          OSR.OSC_num_perfect_matches++;
        OSR.OSC_num_matches++;
      }
    }
  }

  if (OSR.OSC_num_matches == 0)
    OSR.overall_results = OSSCAN_NOMATCHES;

  return;
}

/* Return true iff s and t are both NULL or both the same string. */
static bool strnulleq(const char *s, const char *t) {
  if (s == NULL && t == NULL)
    return true;
  else if (s == NULL || t == NULL)
    return false;
  else
    return strcmp(s, t) == 0;
}

// Go through any previously entered classes to see if this is a dupe;
bool FingerPrintResults::classAlreadyExistsInResults(struct OS_Classification *OSC) {
  int i;

  for (i=0; i < OSR.OSC_num_matches; i++) {
    if (strnulleq(OSC->OS_Vendor, OSR.OSC[i]->OS_Vendor) &&
        strnulleq(OSC->OS_Family, OSR.OSC[i]->OS_Family) &&
        strnulleq(OSC->Device_Type, OSR.OSC[i]->Device_Type) &&
        strnulleq(OSC->OS_Generation, OSR.OSC[i]->OS_Generation)) {
    // Found a duplicate!
    return true;
    }
  }

  // Went through all the results -- no duplicates found
  return false;
}