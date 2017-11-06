#define STATUS_RUNNING		1
#define STATUS_PAUSED		2
#define STATUS_CANCELLED	3

bool operations_init(void);
void operations_term(void);

int operation_start(void);
bool operation_progress(unsigned operation_id);
void operation_end(unsigned operation_id);

void operation_pause(unsigned operation_id);
void operation_resume(unsigned operation_id);
void operation_cancel(unsigned operation_id);
