struct search_entry
{
	char *path;
#ifdef OS_BSD
	struct stat info;
#else
	struct _stati64 info;
#endif
};

#ifdef OS_BSD
struct vector *restrict search_index_results(const struct string *root, const struct string *name, bool case_sensitive);
# define search_index_results(root, name) (search_index_results)((root), (name), false)
void search_index_free(struct vector *restrict result);
#else
struct vector *search_index_results(wchar_t *path,wchar_t *name);
void search_index_free(struct vector *vec_res);
#endif
