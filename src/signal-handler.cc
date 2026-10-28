/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "signal-handler.h"

#include <pthread.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace
{

void write_error(const char *text)
{
	// Diagnostics are best effort; do not enter buffered I/O or application logging.
	[[maybe_unused]] const auto written = write(STDERR_FILENO, text, strlen(text));
}

const char *signal_name(int signo)
{
	switch (signo)
		{
		case SIGABRT: return "Abort"; // NOTRANSLATE: Fatal-signal diagnostic.
		case SIGBUS: return "Bus error"; // NOTRANSLATE: Fatal-signal diagnostic.
		case SIGFPE: return "Floating-point exception"; // NOTRANSLATE: Fatal-signal diagnostic.
		case SIGILL: return "Illegal instruction"; // NOTRANSLATE: Fatal-signal diagnostic.
		case SIGSEGV: return "Invalid memory reference"; // NOTRANSLATE: Fatal-signal diagnostic.
		default: return "Unknown signal"; // NOTRANSLATE: Fatal-signal diagnostic.
		}
}

void fatal_signal_cb(int signo, siginfo_t *info, void *)
{
	write_error("Geeqie fatal error\nSignal: "); // NOTRANSLATE: Fatal-signal diagnostic.
	write_error(signal_name(signo));
	write_error("\n");
	// User-generated signals do not carry a fault address in si_addr.
	if (info && info->si_code > 0 && signo != SIGABRT)
		{
		if (signo == SIGSEGV)
			{
			if (info->si_code == SEGV_MAPERR) write_error("Code: Address not mapped\n"); // NOTRANSLATE: Fatal-signal diagnostic.
			else if (info->si_code == SEGV_ACCERR) write_error("Code: Invalid permissions\n"); // NOTRANSLATE: Fatal-signal diagnostic.
			}
		write_error("Address: 0x"); // NOTRANSLATE: Fatal-signal diagnostic.
		constexpr char digits[] = "0123456789abcdef"; // NOTRANSLATE: Hexadecimal digits.
		char address[(sizeof(uintptr_t) * 2) + 1]{};
		auto value = reinterpret_cast<uintptr_t>(info->si_addr);
		for (size_t i = sizeof(address) - 1; i > 0; --i)
			{
			address[i - 1] = digits[value & 0xf];
			value >>= 4;
			}
		write_error(address);
		write_error("\n");
		}

	// exit(), logging, and stack unwinding can run allocators or destructors
	// concurrently with interrupted application code. Leave cleanup to the OS.
	// Re-deliver the original signal to this thread, preserving core-dump behavior.
	struct sigaction action{};
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	if (sigaction(signo, &action, nullptr) == 0)
		{
		sigset_t unblocked;
		sigemptyset(&unblocked);
		sigaddset(&unblocked, signo);
		if (pthread_sigmask(SIG_UNBLOCK, &unblocked, nullptr) == 0) raise(signo);
		}
	_exit(128 + signo);
}

} // namespace

void setup_sig_handler()
{
	struct sigaction action{};
	sigfillset(&action.sa_mask);
	action.sa_sigaction = fatal_signal_cb;
	action.sa_flags = SA_SIGINFO | SA_RESETHAND;
	for (int signo : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV})
		{
		sigaction(signo, &action, nullptr);
		}
}
