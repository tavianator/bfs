#include <stddef.h>
#include <sys/types.h>
#include <sys/acl.h>

int main(void) {
	acl_t acl = acl_get_file(".", ACL_TYPE_DEFAULT);
	return acl == (acl_t)NULL;
}
