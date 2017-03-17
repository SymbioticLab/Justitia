//
//  main3.cpp
//  parse_data
//
//  Created by Yiwen Zhang on 3/01/17.
//  Copyright © 2017 Yiwen Zhang. All rights reserved.
//

#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <stdlib.h>
#include <algorithm>

using namespace std;

int main(int argc, const char * argv[]) {
    //For Xcode input redirection ONLY:
    //if (getenv("EnVar")) {
    //    freopen(getenv("EnVar"), "r", stdin);
    //}
    
    string line;
    vector<float> latency;
    float data1, data2, data3, data4;
    getline(cin,line);  // skip the first line
    
    long num_data = 0;
    while (getline(cin, line)) {
        stringstream sstr(line);
        sstr >> data1 >> data2 >> data3 >> data4;
        latency.push_back(data3);
        num_data++;
    }

    sort(latency.begin(), latency.end());
    // find median latency
    float median_lat = -1;
    if (latency.size() % 2 == 0) {
        int n = latency.size() / 2;
        median_lat = (float)(latency[n - 1] + latency[n]) / (float)2;
    } else {
        int n = latency.size() / 2 + 1;
        median_lat = latency[n];
    }
    
    cout << "median latency: " << median_lat << endl;
    cout << "total sample points: " << latency.size() << endl;
    // find 99th percentile
    unsigned int n99 = latency.size() * 0.99 - 1;
    cout << "99th index: " << n99 << endl;
    float p99 = latency[n99];
    cout << "99th percentile latency: " << p99 << endl;
    unsigned int n9999 = latency.size() * 0.9999 - 1;
    cout << "9999th index: " << n9999 << endl;
    float p9999 = latency[n9999];
    cout << "99.99th percentile latency: " << p9999 << endl;
    return 0;
}
