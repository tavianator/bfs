#include <selinux/selinux.h>

int main(void) {
	freecon(0);
	return 0;
}
