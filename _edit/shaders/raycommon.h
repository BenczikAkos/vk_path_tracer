struct hitPayload
{
  vec3 hitValue;      // Accumulated color
  vec3 rayOrigin;     // Current ray origin
  vec3 rayDir;        // Current ray direction
  vec3 throughput;    // Current path throughput
  int depth;          // Current path depth
  bool done;          // Whether path should terminate
};
