/*

    the JSON object accuchek prints on stdout (format 2, see
    schema/output.schema.json), built in memory so that it is written in one
    go, or not at all

 */

#ifndef __OUTPUT_H__
    #define __OUTPUT_H__

    #include <session.h>
    #include <string>
    #include <vector>

    namespace accuchek {

    std::string outputJson(const SessionReport &report, const std::vector<Sample> &samples);

    } // namespace accuchek

#endif // __OUTPUT_H__
