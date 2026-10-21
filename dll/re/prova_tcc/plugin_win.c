void ExitProcess(unsigned);
void *ck3_plugin_start(void *a) { ExitProcess(1); return 0; }
