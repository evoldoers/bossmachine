#include <iostream>
#include <string>
#include "../../src/fastseq.h"
#include "../../src/regexmacros.h"
#include "../../src/util.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../../ext/cpp-httplib/httplib.h"

using namespace std;
using namespace MachineBoss;

const char* host = "www.uniprot.org";
const int port = 443;

const char* prefix = "/uniprot/";
const char* suffix = ".fasta";

int main (int argc, char** argv)
{
  if (argc != 2) {
    cerr << "Usage: " << argv[0] << " <Uniprot_ID>" << endl;
    exit(1);
  }

  httplib::SSLClient cli (host, port);
  
  const string path = string(prefix) + argv[1] + suffix;
  cerr << "Path: " << path << endl;
  auto res = cli.Get (path.c_str());

  if (res) {
    cerr << "Status: " << res->status << endl;
    if (res->has_header("Location"))
      cerr << "Location: " << res->get_header_value("Location") << endl;

    const string fasta (res->body);
    const FastSeq fs = FastSeq::fromFasta (fasta);
    fs.writeFasta (cout);
  }
}
