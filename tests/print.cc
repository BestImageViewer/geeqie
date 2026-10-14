/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "print.h"

#include "gtest/gtest.h"

TEST(PrintProof, FitsCellsAndGapsWithinPrintableArea)
{
	const auto layout = print_proof_layout(540, 720, 144, 144);
	EXPECT_EQ(layout.columns, 3);
	EXPECT_EQ(layout.rows, 4);
	EXPECT_EQ(print_proof_page_count(0, layout), 0);
	EXPECT_EQ(print_proof_page_count(12, layout), 1);
	EXPECT_EQ(print_proof_page_count(13, layout), 2);
	EXPECT_EQ(print_proof_page_count(24, layout), 2);
}

TEST(PrintProof, IncludesExactBoundaryAndReservesPageText)
{
	const auto exact = print_proof_layout(293, 293, 144, 144);
	EXPECT_EQ(exact.columns, 2);
	EXPECT_EQ(exact.rows, 2);
	const auto caption = print_proof_layout(293, 273, 144, 144);
	EXPECT_EQ(caption.columns, 2);
	EXPECT_EQ(caption.rows, 1);
	EXPECT_EQ(print_proof_page_count(3, caption), 2);
}

TEST(PrintProof, OversizedProofStillFitsOneImage)
{
	const auto layout = print_proof_layout(200, 300, 720, 720);
	EXPECT_EQ(layout.columns, 1);
	EXPECT_EQ(layout.rows, 1);
	EXPECT_DOUBLE_EQ(layout.cell_width, 200);
	EXPECT_DOUBLE_EQ(layout.cell_height, 300);
	EXPECT_EQ(print_proof_page_count(3, layout), 3);
}
