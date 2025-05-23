/*
 * Copyright 2020-2025 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "WhileDoStmt.h"

#include "AstVisitor.h"
#include "build.h"
#include "CForLoop.h"
#include "codegen.h"
#include "LayeredValueTable.h"
#include "llvmTracker.h"
#include "llvmVer.h"

#ifdef HAVE_LLVM
#include "llvm/IR/Module.h"
#endif


/************************************ | *************************************
*                                                                           *
* Instance methods                                                          *
*                                                                           *
************************************* | ************************************/

GenRet WhileDoStmt::codegen()
{
  GenInfo* info    = gGenInfo;
  FILE*    outfile = info->cfile;
  GenRet   ret;

  codegenStmt(this);
  reportVectorizable();

  if (outfile)
  {
    std::string hdr = "while (" + codegenValue(condExprGet()).c + ") ";

    info->cStatements.push_back(hdr);

    if (this != getFunction()->body)
      info->cStatements.push_back("{\n");

    body.codegen("");

    if (this != getFunction()->body)
    {
      std::string end  = "}";
      CondStmt*   cond = toCondStmt(parentExpr);

      if (cond == 0 || !(cond->thenStmt == this && cond->elseStmt))
        end += "\n";

      info->cStatements.push_back(end);
    }
  }

  else
  {
#ifdef HAVE_LLVM
    llvm::Function*   func          = info->irBuilder->GetInsertBlock()->getParent();
    llvm::BasicBlock* blockStmtBody = NULL;
    llvm::BasicBlock* blockStmtEnd  = NULL;
    llvm::BasicBlock* blockStmtCond = NULL;

    getFunction()->codegenUniqueNum++;

    blockStmtBody = llvm::BasicBlock::Create(info->module->getContext(), FNAME("blk_body"));
    blockStmtEnd  = llvm::BasicBlock::Create(info->module->getContext(), FNAME("blk_end"));
    blockStmtCond = llvm::BasicBlock::Create(info->module->getContext(), FNAME("blk_cond"));
    trackLLVMValue(blockStmtBody);
    trackLLVMValue(blockStmtEnd);
    trackLLVMValue(blockStmtCond);

#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockStmtCond);
#else
    func->getBasicBlockList().push_back(blockStmtCond);
#endif

    // Insert an explicit branch from the current block to the loop start.
    llvm::BranchInst* toStart = info->irBuilder->CreateBr(blockStmtCond);
    trackLLVMValue(toStart);

    // Now switch to the condition for code generation
    info->irBuilder->SetInsertPoint(blockStmtCond);

    GenRet            condValueRet     = codegenValue(condExprGet());
    llvm::Value*      condValue        = condValueRet.val;

    if (condValue->getType() != llvm::Type::getInt1Ty(info->module->getContext()))
    {
      condValue = info->irBuilder->CreateICmpNE(condValue,
                                                llvm::ConstantInt::get(condValue->getType(), 0),
                                                FNAME("condition"));
      trackLLVMValue(condValue);
    }

    // Now we might go either to the Body or to the End.
    llvm::BranchInst* condBr = info->irBuilder->CreateCondBr(condValue, blockStmtBody, blockStmtEnd);
    trackLLVMValue(condBr);

    // Now add the body.
#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockStmtBody);
#else
    func->getBasicBlockList().push_back(blockStmtBody);
#endif

    info->irBuilder->SetInsertPoint(blockStmtBody);
    info->lvt->addLayer();

    body.codegen("");

    info->lvt->removeLayer();

    if (blockStmtCond) {
      llvm::BranchInst* toCond = info->irBuilder->CreateBr(blockStmtCond);
      trackLLVMValue(toCond);
    } else {
      llvm::BranchInst* toEnd = info->irBuilder->CreateBr(blockStmtEnd);
      trackLLVMValue(toEnd);
    }

#if HAVE_LLVM_VER >= 160
    func->insert(func->end(), blockStmtEnd);
#else
    func->getBasicBlockList().push_back(blockStmtEnd);
#endif

    info->irBuilder->SetInsertPoint(blockStmtEnd);

    if (blockStmtCond) INT_ASSERT(blockStmtCond->getParent() == func);
    if (blockStmtBody) INT_ASSERT(blockStmtBody->getParent() == func);
    if (blockStmtEnd)  INT_ASSERT(blockStmtEnd->getParent()  == func);
#endif
  }

  return ret;
}
