#include <sys/types.h>
#include <sys/acl.h>

int main(void) {
	acl_t acl = acl_get_file(".", ACL_TYPE_DEFAULT);
	acl_entry_t entry;
	return acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
}
