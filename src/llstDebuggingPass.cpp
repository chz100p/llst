#include <llstDebuggingPass.h>
#include <jit.h> //TObjectTypes

using namespace llvm;

typedef std::vector<Instruction*>   InstructionVector;
typedef std::set<Instruction*>      InstructionSet;
typedef InstructionSet::iterator    InstructionSI;

namespace {
    struct LLSTDebuggingPass : public FunctionPass {
    public:
        virtual bool runOnFunction(Function &F) ;
        static char ID;
        LLSTDebuggingPass(): FunctionPass(ID) { }
        bool belongsToSmalltalkType(Type* type);
        ~LLSTDebuggingPass() {
            delete m_builder;
        }
    private:
        TObjectTypes m_baseTypes;
        Module* m_module;
        Constant* _printf;
        IRBuilder<>* m_builder;
        Function* isSmallInteger;
        Function* getObjectField;
        Function* getObjectClass;
        
        
        
        void initFromFunction(Function& F);
        void insertSelfInSendMessageCheck(Function& F);
        void insertLoadInstCheck(Function& F);
    };
}

char LLSTDebuggingPass::ID = 0;
static RegisterPass<LLSTDebuggingPass> LLSTSpecificDebuggingPass("llst-debug", "Pass to debug LLST", false /* Only looks at CFG */, false /* Analysis Pass */);

FunctionPass* createLLSTDebuggingPass() {
  return new LLSTDebuggingPass();
}

bool LLSTDebuggingPass::belongsToSmalltalkType(Type* type)
{
    if (   type == m_baseTypes.block->getPointerTo()
        || type == m_baseTypes.byteObject->getPointerTo()
        || type == m_baseTypes.process->getPointerTo()
        || type == m_baseTypes.object->getPointerTo()
        || type == m_baseTypes.objectArray->getPointerTo()
        || type == m_baseTypes.symbol->getPointerTo()
        || type == m_baseTypes.symbolArray->getPointerTo()
        || type == m_baseTypes.dictionary->getPointerTo()
        || type == m_baseTypes.method->getPointerTo()
        || type == m_baseTypes.context->getPointerTo()
        || type == m_baseTypes.klass->getPointerTo()
    )
        return true;
    return false;
}

void LLSTDebuggingPass::initFromFunction(Function& F)
{
    m_module = F.getParent();
    m_baseTypes.initializeFromModule(m_module);
    m_builder = new IRBuilder<>(F.begin());
    
    FunctionType* _printfType = FunctionType::get(m_builder->getInt32Ty(), m_builder->getInt8PtrTy(), true);
     _printf = m_module->getOrInsertFunction("printf", _printfType);
     
    isSmallInteger = m_module->getFunction("isSmallInteger");
    getObjectField = m_module->getFunction("getObjectField");
    getObjectClass = m_module->getFunction("getObjectClass");
}

void LLSTDebuggingPass::insertLoadInstCheck(Function& F)
{
    Value* BrokenPointerMessage = m_builder->CreateGlobalStringPtr("\npointer is broken\n");
    
    InstructionVector Loads;
    for (Function::iterator BB = F.begin(); BB != F.end(); BB++)
    {
        for(BasicBlock::iterator II = BB->begin(); II != BB->end(); II++)
        {
            if (LoadInst* Load = dyn_cast<LoadInst>(II)) {
                Loads.push_back(Load);
            }
        }
    }
    
    for(size_t i = 0; i < Loads.size(); i++)
    {
        LoadInst* Load = dyn_cast<LoadInst>(Loads[i]);
        if (belongsToSmalltalkType( Load->getType() )) {
        
            //split BB right after load inst. The new BB contains code that will be executed if pointer is OK
            BasicBlock* PointerIsOkBB = Load->getParent()->splitBasicBlock(++((BasicBlock::iterator) Load));
            BasicBlock* PointerIsBrokenBB = BasicBlock::Create(m_module->getContext(), "", &F, PointerIsOkBB);
            BasicBlock* PointerIsNotSmallIntBB = BasicBlock::Create(m_module->getContext(), "", &F, PointerIsBrokenBB);
            
            Instruction* branchToPointerIsOkBB = ++((BasicBlock::iterator) Load);
            //branchToPointerIsOkBB is created by splitBasicBlock() just after load inst
            //We force builder to insert instructions before branchToPointerIsOkBB
            m_builder->SetInsertPoint(branchToPointerIsOkBB);
            
            //If pointer to class is null, jump to PointerIsBroken, otherwise to PointerIsOkBB
            Value* objectPtr = m_builder->CreateBitCast( Load, m_baseTypes.object->getPointerTo());
            
            Value* isSmallInt = m_builder->CreateCall(isSmallInteger, objectPtr);
            m_builder->CreateCondBr(isSmallInt, PointerIsOkBB, PointerIsNotSmallIntBB);
            
            m_builder->SetInsertPoint(PointerIsNotSmallIntBB);
            Value* klassPtr = m_builder->CreateCall(getObjectClass, objectPtr);
            Value* pointerIsNull = m_builder->CreateICmpEQ(klassPtr, ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()) );
            m_builder->CreateCondBr(pointerIsNull, PointerIsBrokenBB, PointerIsOkBB);
            
            branchToPointerIsOkBB->eraseFromParent(); //We don't need it anymore
            
            m_builder->SetInsertPoint(PointerIsBrokenBB);
            m_builder->CreateCall(_printf, BrokenPointerMessage);
            m_builder->CreateBr(PointerIsOkBB);
        }
    }
}

void LLSTDebuggingPass::insertSelfInSendMessageCheck(Function& F)
{
    Value* BrokenSelfMessage = m_builder->CreateGlobalStringPtr("\nself is broken\n");
    
    InstructionVector Calls;
    for (Function::iterator BB = F.begin(); BB != F.end(); BB++)
    {
        for(BasicBlock::iterator II = BB->begin(); II != BB->end(); II++)
        {
            if (CallInst* Call = dyn_cast<CallInst>(II)) {
                if (Call->getCalledFunction()->getName() == "sendMessage")
                    Calls.push_back(Call);
            }
        }
    }
    
    for(size_t i = 0; i < Calls.size(); i++)
    {
        CallInst* Call = dyn_cast<CallInst>(Calls[i]);
        
        BasicBlock* PointerIsOkBB = Call->getParent()->splitBasicBlock(((BasicBlock::iterator) Call));
        BasicBlock* PointerIsBrokenBB = BasicBlock::Create(m_module->getContext(), "", &F, PointerIsOkBB);
        BasicBlock* PointerIsNotSmallIntBB = BasicBlock::Create(m_module->getContext(), "", &F, PointerIsBrokenBB);
        
        Instruction* branchToPointerIsOkBB = & PointerIsOkBB->getSinglePredecessor()->back();
        m_builder->SetInsertPoint(branchToPointerIsOkBB);
        
        
        Value* argsPtr = m_builder->CreateBitCast( Call->getArgOperand(2), m_baseTypes.object->getPointerTo());
        Value* self = m_builder->CreateCall2(getObjectField, argsPtr, m_builder->getInt32(0));
        
        Value* isSmallInt = m_builder->CreateCall(isSmallInteger, self);
        m_builder->CreateCondBr(isSmallInt, PointerIsOkBB, PointerIsNotSmallIntBB);
        
        
        m_builder->SetInsertPoint(PointerIsNotSmallIntBB);
        Value* klassPtr = m_builder->CreateCall(getObjectClass, self);
        Value* pointerIsNull = m_builder->CreateICmpEQ(klassPtr, ConstantPointerNull::get(m_baseTypes.klass->getPointerTo()) );
        m_builder->CreateCondBr(pointerIsNull, PointerIsBrokenBB, PointerIsOkBB);
        branchToPointerIsOkBB->eraseFromParent();
        
        m_builder->SetInsertPoint(PointerIsBrokenBB);
        m_builder->CreateCall(_printf, BrokenSelfMessage);
        m_builder->CreateBr(PointerIsOkBB);
    }
}



bool LLSTDebuggingPass::runOnFunction(Function& F)
{
    initFromFunction(F);
    insertLoadInstCheck(F);
    insertSelfInSendMessageCheck(F);
    //outs() << F;
    return true;
}