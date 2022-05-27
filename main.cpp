#include <iostream>
#include "FFAudioTranscoder.h"

using namespace std;

int main()
{
    FFAudioTranscoder transcoder("test001.opus", "sound.aac");
    transcoder.open();
    transcoder.transcode();
    return 0;
}

