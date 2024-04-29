#include <string.h>
#include <sys/types.h>
#include <sys/acl.h>

int main(void) {
	acl_entry_t entry;
	memset(&entry, 0, sizeof(entry));
	acl_tag_t tag;
	return acl_get_tag_type(entry, &tag);
}
